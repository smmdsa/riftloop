// RiftLoop.Analyzer: post-match ingest + analysis CLI (PRD 14.2).
// Runs at low priority and never touches the League client.
#include "core/analysis.h"
#include "core/config.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/ingest.h"
#include "core/lcu.h"
#include "core/lcu_history.h"
#include "core/planner.h"
#include "core/meta.h"
#include "core/perkpages.h"
#include "core/replays.h"
#include "core/clipmaker.h"
#include "core/videoframe.h"
#include "core/videocut.h"
#include "core/missions.h"
#include "core/serial.h"
#include "core/util.h"

#include <windows.h>

#include <cstdio>
#include <functional>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace rl;
using nlohmann::json;

namespace {

void usage() {
    std::printf("RiftLoop.Analyzer %s (compilado %s)\n", rl::kAppVersion,
                rl::appBuildStamp());
    std::printf(
        "\n"
        "  --ingest <archivo.json>...   Importa partidas (match-v5 o {match,timeline})\n"
        "  --fetch-lcu [N]              Descarga N partidas desde el cliente de League\n"
        "  --fetch [N]                  Via Riot API (opcional, necesita API key)\n"
        "  --meta [N]                   Refresca la muestra local de builds y runas\n"
        "  --plan <Campeon> <ROL>       Imprime runas e items desde la muestra local\n"
        "  --clips <matchId|last>       Genera clips de evidencia desde el replay\n"
        "  --replay-check               Estado de los replays, sin abrir nada\n"
        "  --autoclip <matchId|last>    Corta la grabacion en clips y borra el raw\n"
        "  --cut-test <mp4> <seg>       Prueba un corte de 20 s desde ese segundo\n"
        "  --frame <mp4> <seg> <png>    Guarda un fotograma para comprobar la sincronia\n"
        "  --probe <mp4>                Duracion y espaciado de keyframes\n"
        "  --apply-runes <Camp> <ROL>   Escribe la pagina de RiftLoop en el cliente\n"
        "  --undo-runes                 Deshace la ultima escritura de runas\n"
        "  --analyze                    Analiza las partidas pendientes\n"
        "  --show <matchId>             Muestra el analisis de una partida\n"
        "  --report                     Resumen: ultima partida, mision, racha\n"
        "  --set-key <RGAPI-...>        Guarda tu API key local (DPAPI)\n"
        "  --set-riot-id <Nombre#TAG>   Define tu Riot ID (para --fetch)\n"
        "  --wipe                       Borra todos los datos locales\n");
}

void printAnalysis(const AnalysisResult& a) {
    std::printf("=== Analisis %s (ruleset %s) ===\n", a.matchId.c_str(), a.rulesetVersion.c_str());
    std::printf("\n[Fortaleza]\n  %s\n", a.strength.c_str());
    int shown = 0;
    for (auto& f : a.findings) {
        if (f.failures == 0) continue;
        if (++shown > 3) break;
        std::printf("\n[%s] %s  (conf: %s, oportunidades: %d, fallos: %d)\n",
                    f.detectorId.c_str(), f.title.c_str(), f.confidence.c_str(),
                    f.opportunities, f.failures);
        std::printf("  Por que importa: %s\n", f.whyItMatters.c_str());
        std::printf("  Alternativa: %s\n", f.alternative.c_str());
        for (auto& ev : f.evidence) {
            std::printf("  - [%s] %s\n", util::formatGameClock(ev.gameTimestampMs).c_str(),
                        ev.observedFacts.c_str());
            std::printf("    Inferencia (%s): %s\n", ev.confidence.c_str(), ev.inference.c_str());
        }
    }
    if (shown == 0) std::printf("\nSin patrones con fallos en esta partida.\n");
    std::printf("\nLimitaciones: %s\n", a.limitations.c_str());
}

int ingestFiles(Db& db, const std::vector<std::string>& files) {
    int ok = 0;
    for (auto& f : files) {
        std::string text = util::readFile(f);
        if (text.empty()) {
            std::printf("error: no se pudo leer %s\n", f.c_str());
            continue;
        }
        auto pair = splitImport(text);
        if (!pair) {
            std::printf("error: %s no es un match-v5 ni un export {match,timeline}\n", f.c_str());
            continue;
        }
        auto m = parseMatch(pair->matchJson);
        if (!m) {
            std::printf("error: %s no se pudo parsear\n", f.c_str());
            continue;
        }
        if (db.upsertMatch(*m, pair->matchJson, pair->timelineJson)) {
            std::printf("importada %s (%s, %s)\n", m->matchId.c_str(), m->patch.c_str(),
                        m->queue.c_str());
            ++ok;
        } else {
            std::printf("ya existia %s\n", m->matchId.c_str());
        }
    }
    return ok;
}

int fetchMatches(Db& db, int count) {
    std::string key = Config::loadApiKey();
    if (key.empty()) {
        std::printf("error: no hay API key. Guarda una con --set-key (developer.riotgames.com).\n");
        return 1;
    }
    Profile prof = db.loadProfile();
    RiotApiConfig api;
    api.apiKey = key;
    api.routing = Config::load().routing;
    std::string err;

    std::string puuid = prof.puuid;
    if (puuid.empty()) {
        auto hash = prof.riotId.find('#');
        if (hash == std::string::npos) {
            std::printf("error: define tu Riot ID con --set-riot-id Nombre#TAG\n");
            return 1;
        }
        std::string body = fetchPuuidByRiotId(api, prof.riotId.substr(0, hash),
                                              prof.riotId.substr(hash + 1), &err);
        if (body.empty()) {
            std::printf("error al resolver Riot ID: %s\n", err.c_str());
            return 1;
        }
        puuid = json::parse(body).value("puuid", "");
        prof.puuid = puuid;
        db.saveProfile(prof);
    }

    std::string idsBody = fetchMatchIds(api, puuid, count, &err);
    if (idsBody.empty()) {
        std::printf("error al listar partidas: %s\n", err.c_str());
        return 1;
    }
    int ok = 0;
    for (auto& idJson : json::parse(idsBody)) {
        std::string id = idJson.get<std::string>();
        if (db.hasMatch(id)) continue;
        std::string mBody = fetchMatch(api, id, &err);
        std::string tBody = fetchTimeline(api, id, &err);
        if (mBody.empty() || tBody.empty()) {
            std::printf("aviso: %s incompleta (%s); continuo\n", id.c_str(), err.c_str());
            Sleep(1500);             // respect rate limits (PRD 14.6)
            continue;
        }
        auto m = parseMatch(mBody);
        if (m && db.upsertMatch(*m, mBody, tBody)) {
            std::printf("descargada %s\n", id.c_str());
            ++ok;
        }
        Sleep(1500);
    }
    std::printf("%d partidas nuevas.\n", ok);
    return 0;
}

void report(Db& db) {
    auto rows = db.listMatches(5);
    std::printf("Partidas almacenadas (ultimas %zu):\n", rows.size());
    for (auto& r : rows)
        std::printf("  %s  %-10s %-8s %s %s\n", r.matchId.c_str(), r.userChampion.c_str(),
                    r.userRole.c_str(), r.userWin ? "WIN " : "LOSS",
                    r.analyzed ? "[analizada]" : "[pendiente]");
    if (auto prog = activeMissionProgress(db)) {
        std::printf("\nMision activa: %s\n  Metrica: %s\n  Progreso: %d/%d partidas, "
                    "%d de %d oportunidades ejecutadas\n",
                    prog->mission.name.c_str(), prog->mission.metric.c_str(),
                    prog->gamesTracked, prog->mission.blockSize, prog->successes,
                    prog->validOpportunities);
    } else {
        std::printf("\nSin mision activa.\n");
    }
    std::printf("Racha de mejora: %d dia(s). XP total: %d.\n", db.streakDays(), db.totalXp());
}

} // namespace

// ---------------------------------------------------------------- clips

// Runs RiftLoop.Capture for one clip and waits for it. Returns false when the
// recorder could not produce the file.
bool recordClip(const std::wstring& outPath, int seconds) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    std::wstring cmd = L"\"" + dir + L"\\RiftLoop.Capture.exe\" --seconds " +
                       std::to_wstring(seconds) + L" --out \"" + outPath + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    // Capture exits on its own after --seconds; the margin covers startup.
    WaitForSingleObject(pi.hProcess, (DWORD)(seconds + 30) * 1000);
    CloseHandle(pi.hProcess);
    return fs::exists(outPath);
}

// Waits until `probe` returns true or the timeout elapses. Polls at 1 s, well
// above the 250 ms floor of PRD 15.4.
bool waitFor(const std::function<bool()>& probe, int timeoutSec, const char* what) {
    for (int i = 0; i < timeoutSec; ++i) {
        if (probe()) return true;
        if (i % 5 == 0 && i > 0) std::printf("  esperando %s (%ds)...\n", what, i);
        Sleep(1000);
    }
    return false;
}

int makeClips(Db& db, Lcu& lcu, const std::string& matchId) {
    Config cfg = Config::load();
    if (!cfg.clipsEnabled) {
        std::printf("Los clips de evidencia estan desactivados. Actívalos en Ajustes: "
                    "generar un clip abre el juego para reproducir tu replay.\n");
        return 2;
    }
    auto analysis = db.loadAnalysis(matchId);
    if (!analysis) {
        std::printf("error: no hay analisis de %s. Ejecuta --analyze primero.\n", matchId.c_str());
        return 2;
    }
    auto plan = planClips(*analysis);
    if (plan.empty()) {
        std::printf("Esa partida no tiene evidencias con instante: no hay nada que grabar.\n");
        return 0;
    }
    if (!lcu.connected()) {
        std::printf("error: el cliente de League no esta abierto.\n");
        return 2;
    }
    ReplayConfig rc = replayConfig(lcu);
    if (!rc.enabled || !rc.forMatchHistory) {
        std::printf("error: el cliente tiene los replays desactivados.\n");
        return 2;
    }
    if (rc.playingGame) {
        std::printf("Hay una partida en curso: no se toca nada (PRD 14.4).\n");
        return 2;
    }

    int64_t gameId = gameIdOfMatch(matchId);
    if (gameId <= 0) {
        std::printf("error: no se pudo sacar el gameId de %s.\n", matchId.c_str());
        return 2;
    }

    db.audit("clips_started", json{{"match", matchId}, {"clips", plan.size()}}.dump());

    if (replayMetadata(lcu, gameId).state != ReplayState::Available) {
        std::printf("Pidiendo el replay al cliente...\n");
        requestReplayDownload(lcu, gameId);
        if (!waitFor([&] { return replayMetadata(lcu, gameId).state == ReplayState::Available; },
                     180, "la descarga del replay")) {
            std::printf("El cliente no dejo el replay listo. Puede que ya no lo conserve "
                        "(caduca) o que el parche no sea compatible.\n");
            return 1;
        }
    }

    std::printf("Abriendo el replay. El juego va a arrancar; no lo cierres.\n");
    if (!watchReplay(lcu, gameId)) {
        std::printf("error: el cliente rechazo abrir el replay.\n");
        return 1;
    }
    if (!waitFor([] { return replayPlayback().valid; }, 150, "que cargue el replay")) {
        std::printf("El replay no respondio en 127.0.0.1:2999.\n");
        return 1;
    }

    fs::path clipDir = util::clipDirFor(matchId);
    std::error_code ec;
    fs::create_directories(clipDir, ec);

    // A clip is only useful if it shows the moment it claims to show. The
    // timeline clock and the replay clock are not guaranteed to share an
    // origin, so every seek is read back and the drift is recorded.
    const double kSeekToleranceSec = 5.0;
    ReplayPlayback first = replayPlayback();
    int made = 0, skipped = 0;
    for (auto& c : plan) {
        double target = (double)c.startMs / 1000.0;
        std::printf("Clip %s en %s...\n", c.detectorId.c_str(),
                    util::formatGameClock(c.gameTimestampMs).c_str());
        if (first.lengthSec > 0 && target > first.lengthSec) {
            std::printf("  ese instante cae fuera del replay (%.0f s); se omite.\n",
                        first.lengthSec);
            ++skipped;
            continue;
        }
        double reached = -1;
        if (!seekAndConfirm(target, kSeekToleranceSec, &reached)) {
            std::printf("  el replay no llego a ese instante (pedido %.0f s, quedo en %.0f s); "
                        "se omite en vez de grabar otro momento.\n", target, reached);
            ++skipped;
            continue;
        }
        // Keep the measured drift: it is the number that tells whether the two
        // clocks share an origin on this client.
        db.setKv("replay_seek_drift_ms", std::to_string((int64_t)((reached - target) * 1000)));
        if (!FindWindowW(nullptr, L"League of Legends (TM) Client")) {
            std::printf("  la ventana del juego no esta; se omite.\n");
            ++skipped;
            continue;
        }
        fs::path out = clipDir / c.fileName;
        if (!recordClip(out.wstring(), c.seconds)) {
            std::printf("  la grabacion fallo; se omite.\n");
            ++skipped;
            continue;
        }
        // Link the clip back to its evidence.
        for (auto& f : analysis->findings)
            for (auto& ev : f.evidence)
                if (ev.evidenceId == c.evidenceId) ev.clipFile = c.fileName;
        ++made;
    }
    if (made > 0) db.saveAnalysis(*analysis);
    // Leave the replay paused: it is the user's game window, not ours.
    pauseReplay();
    db.audit("clips_done",
             json{{"match", matchId}, {"made", made}, {"skipped", skipped}}.dump());
    std::printf("%d clips en %s (%d omitidos). El replay queda en pausa.\n",
                made, clipDir.string().c_str(), skipped);
    return made > 0 ? 0 : 1;
}


// Thin wrapper over the shared implementation (core/clipmaker.h) so the CLI
// and the Desktop cut clips with exactly the same code.
int autoClip(Db& db, const std::string& matchId) {
    auto res = makeClipsFromRecording(db, matchId, [](ClipProgress p) {
        std::printf("  [%d/%d] %s\n", p.step, p.total, p.label.c_str());
    });
    std::printf("%s\n", res.message.c_str());
    return res.ok ? 0 : 1;
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);   // PRD 14.2

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) { usage(); return 0; }

    try {
        Db db;
        std::string cmd = args[0];

        if (cmd == "--set-key" && args.size() >= 2) {
            Config::saveApiKey(args[1]);
            std::printf("API key guardada (protegida con DPAPI).\n");
            return 0;
        }
        if (cmd == "--set-riot-id" && args.size() >= 2) {
            Profile p = db.loadProfile();
            p.riotId = args[1];
            p.puuid.clear();         // re-resolve on next fetch
            db.saveProfile(p);
            std::printf("Riot ID: %s\n", args[1].c_str());
            return 0;
        }
        if (cmd == "--wipe") {
            db.wipeAll();
            std::printf("Datos locales eliminados.\n");
            return 0;
        }
        if (cmd == "--ingest" && args.size() >= 2) {
            std::vector<std::string> files(args.begin() + 1, args.end());
            int n = ingestFiles(db, files);
            std::printf("%d partidas importadas. Ejecuta --analyze para procesarlas.\n", n);
            return 0;
        }
        if (cmd == "--fetch") {
            int n = args.size() >= 2 ? std::atoi(args[1].c_str()) : Config::load().matchImportCount;
            return fetchMatches(db, n > 0 ? n : 20);
        }

        // Data Dragon is needed from here on (localized to the client).
        Lcu lcu;
        lcu.connect(Config::load().leagueLockfilePath);
        std::string locale = resolveDataLocale(db, lcu.connected() ? &lcu : nullptr,
                                               Config::load().dataLocale);
        Ddragon dd;
        bool ddOk = dd.load(true, locale);
        if (!ddOk) std::printf("aviso: Data Dragon no disponible (sin red y sin cache); "
                               "el analisis pierde D09 y validaciones de parche.\n");

        if (cmd == "--fetch-lcu") {
            int count = args.size() >= 2 ? std::atoi(args[1].c_str())
                                         : Config::load().matchImportCount;
            auto r = importFromClient(db, lcu, ddOk ? &dd : nullptr,
                                      count > 0 ? count : 20);
            if (!r.error.empty()) {
                std::printf("error: %s\n", r.error.c_str());
                return 1;
            }
            std::printf("%d partidas importadas desde el cliente, %d ya existian u "
                        "omitidas. Ejecuta --analyze.\n", r.imported, r.skipped);
            return 0;
        }
        if (cmd == "--auto") {
            // Agent-invoked mode: client history first, API key as fallback.
            if (lcu.connected()) {
                auto r = importFromClient(db, lcu, ddOk ? &dd : nullptr, 5);
                if (!r.error.empty() && !Config::loadApiKey().empty())
                    fetchMatches(db, 5);
            } else if (!Config::loadApiKey().empty()) {
                fetchMatches(db, 5);
            }
            int done = analyzePending(db, ddOk ? &dd : nullptr);
            // A finished analysis knows which moments matter, so the recording
            // can become clips right away and stop costing a gigabyte
            // (RF-REC-003). Failures here never block the analysis.
            if (done > 0 && Config::load().captureEnabled) {
                auto rows = db.listMatches(1);
                if (!rows.empty()) autoClip(db, rows[0].matchId);
            }
            return 0;
        }
        if (cmd == "--apply-runes") {
            if (argc < 4) { usage(); return 2; }
            if (!Config::load().runeWriteEnabled) {
                std::printf("La escritura de runas esta desactivada (opt-in, PRD 9.5).\n");
                return 2;
            }
            PlanInput pi;
            pi.champion = argv[2];
            pi.role = argv[3];
            pi.draft.role = pi.role;
            for (int k = 4; k < argc; ++k) pi.draft.enemyChampions.push_back(argv[k]);
            if (!ddOk || !dd.champion(pi.champion)) {
                std::printf("error: campeon desconocido en el parche instalado\n");
                return 2;
            }
            RunePlan rp = planRunes(db, dd, pi);
            std::string name = "RiftLoop: " + pi.champion + " " + pi.role;
            auto pages = parsePerkPages(lcu.getRaw("/lol-perks/v1/pages"));
            auto inv = parsePerkInventory(lcu.getRaw("/lol-perks/v1/inventory"));
            PageWritePlan plan = planPageWrite(pages, inv, rp.main, name,
                                               db.getKv("rune_write_signature"), dd);
            std::printf("Pagina: %s\n", name.c_str());
            std::printf("Accion: %s\n",
                        plan.action == WriteAction::Create      ? "crear"
                        : plan.action == WriteAction::Overwrite ? "sobrescribir la nuestra"
                        : plan.action == WriteAction::NoChange  ? "ninguna, ya aplicada"
                                                                : "bloqueada");
            for (auto& d2 : plan.diff) std::printf("  %s\n", d2.c_str());
            if (!plan.blockedReason.empty())
                std::printf("  motivo: %s\n", plan.blockedReason.c_str());
            WriteResult r = applyRunePage(db, lcu, dd, rp.main, name, false);
            std::printf("%s\n", r.message.c_str());
            return r.ok ? 0 : 1;
        }
        if (cmd == "--undo-runes") {
            WriteResult r = undoRunePage(db, lcu);
            std::printf("%s\n", r.message.c_str());
            return r.ok ? 0 : 1;
        }
        if (cmd == "--autoclip") {
            std::string id = argc > 2 ? argv[2] : "last";
            if (id == "last") {
                auto rows = db.listMatches(1);
                if (rows.empty()) {
                    std::printf("error: no hay partidas importadas.\n");
                    return 2;
                }
                id = rows[0].matchId;
            }
            return autoClip(db, id);
        }
        if (cmd == "--probe") {
            if (argc < 3) { usage(); return 2; }
            KeyframeReport kr = probeKeyframes(util::widen(argv[2]));
            std::printf("duracion %.1f s, %d muestras, %d keyframes\n", kr.durationSec,
                        kr.samples, kr.keyframes);
            std::printf("espaciado de keyframes: medio %.1f s, maximo %.1f s\n",
                        kr.meanGapSec, kr.maxGapSec);
            return 0;
        }
        if (cmd == "--frame") {
            if (argc < 5) { usage(); return 2; }
            std::wstring src = util::widen(argv[2]);
            double at = std::atof(argv[3]);
            // Full size: the game clock in the corner has to stay readable.
            HBITMAP bmp = grabFrame(src, at, 1280, 720);
            if (bmp) {
                BITMAP info{};
                GetObject(bmp, sizeof info, &info);
                std::printf("bitmap %ldx%ld, %d bpp, stride %ld\n", info.bmWidth,
                            info.bmHeight, info.bmBitsPixel, info.bmWidthBytes);
            }
            if (!bmp) {
                std::printf("error: no se pudo extraer el fotograma\n");
                return 1;
            }
            bool ok = saveBitmapPng(bmp, util::widen(argv[4]));
            DeleteObject(bmp);
            std::printf("%s\n", ok ? argv[4] : "error: no se pudo guardar el png");
            return ok ? 0 : 1;
        }
        if (cmd == "--cut-test") {
            if (argc < 4) { usage(); return 2; }
            std::wstring src = util::widen(argv[2]);
            double at = std::atof(argv[3]);
            std::printf("duracion de la grabacion: %.1f s\n",
                        mediaDurationSec(src));
            CutRequest c;
            c.startSec = at;
            c.durationSec = 20;
            c.outPath = (util::clipsDir() / "cuttest.mp4").wstring();
            auto t0 = GetTickCount64();
            CutResult r = cutClips(src, {c});
            std::printf("cortes: %d en %llu ms  %s\n", r.written,
                        (unsigned long long)(GetTickCount64() - t0), r.error.c_str());
            if (r.written > 0)
                std::printf("clip: %s (%.1f s)\n",
                            util::narrow(c.outPath).c_str(), mediaDurationSec(c.outPath));
            return r.written > 0 ? 0 : 1;
        }
        if (cmd == "--replay-check") {
            // Read-only diagnosis: it opens no game and downloads nothing.
            Config cfg = Config::load();
            std::printf("Clips de evidencia: %s\n",
                        cfg.clipsEnabled ? "activados" : "desactivados (opt-in, PRD 9.10)");
            if (!lcu.connected()) {
                std::printf("Cliente de League: cerrado\n");
                return 1;
            }
            ReplayConfig rc = replayConfig(lcu);
            std::printf("Replays habilitados: %s | desde historial: %s | partida en curso: %s\n",
                        rc.enabled ? "si" : "no", rc.forMatchHistory ? "si" : "no",
                        rc.playingGame ? "si" : "no");
            std::printf("Carpeta de replays: %s\n", replayFolder(lcu).c_str());
            std::string drift = db.getKv("replay_seek_drift_ms");
            std::printf("Desfase de seek medido: %s\n",
                        drift.empty() ? "sin medir (no se ha generado ningun clip)" : drift.c_str());
            ReplayPlayback pb = replayPlayback();
            std::printf("Replay API (127.0.0.1:2999): %s\n",
                        pb.valid ? "responde" : "no responde (no hay replay abierto)");
            for (auto& row : db.listMatches(5)) {
                int64_t gid = gameIdOfMatch(row.matchId);
                ReplayMetadata md = replayMetadata(lcu, gid);
                const char* st = md.state == ReplayState::Available     ? "listo"
                               : md.state == ReplayState::Downloading   ? "por descargar"
                               : md.state == ReplayState::NotDownloaded ? "no descargado"
                               : md.state == ReplayState::Lost          ? "no disponible"
                                                                       : "desconocido";
                int clips = 0;
                if (auto a = db.loadAnalysis(row.matchId)) clips = (int)planClips(*a).size();
                std::printf("  %-18s %-14s %d clips planificados\n",
                            row.matchId.c_str(), st, clips);
            }
            return 0;
        }
        if (cmd == "--clips") {
            std::string matchId = argc > 2 ? argv[2] : "last";
            if (matchId == "last") {
                auto rows = db.listMatches(1);
                if (rows.empty()) {
                    std::printf("error: no hay partidas importadas.\n");
                    return 2;
                }
                matchId = rows[0].matchId;
            }
            return makeClips(db, lcu, matchId);
        }
        if (cmd == "--plan") {
            if (argc < 4) { usage(); return 2; }
            PlanInput pi;
            pi.champion = argv[2];
            pi.role = argv[3];
            // Optional enemy picks let the matchup logic be checked without a
            // live champion select.
            for (int k = 4; k < argc; ++k) pi.draft.enemyChampions.push_back(argv[k]);
            pi.draft.role = pi.role;
            if (!ddOk || !dd.champion(pi.champion)) {
                std::printf("error: campeon desconocido en el parche instalado\n");
                return 2;
            }
            std::string family = util::patchFamily(dd.version());
            std::printf("=== %s %s (parche %s, muestra local: %d builds) ===\n",
                        pi.champion.c_str(), pi.role.c_str(), dd.displayPatch().c_str(),
                        metaSampleSize(db, pi.champion, pi.role, family));
            RunePlan rp = planRunes(db, dd, pi);
            std::printf("estado: %s\n",
                        rp.draftClosed ? "draft cerrado, plan definitivo"
                                       : "draft abierto, plan provisional");
            std::printf("\n[Runas] confianza %s\n", rp.confidence.c_str());
            for (size_t k = 0; k < rp.main.perks.size(); ++k) {
                int perk = rp.main.perks[k];
                std::string name = k >= 6 ? dd.shardName(perk) : dd.perkName(perk);
                std::printf("  %d %s\n", perk,
                            name.c_str());
            }
            for (auto& r : rp.main.reasons) std::printf("  - %s\n", r.c_str());
            if (rp.situational)
                for (auto& r : rp.situational->reasons)
                    std::printf("  [alt] %s\n", r.c_str());
            else
                std::printf("  [alt] sin alternativa: %s\n",
                            rp.noAlternativeReason.c_str());
            ItemPlan ip = planItems(db, dd, pi);
            std::printf("\n[Items] confianza %s\n", ip.confidence.c_str());
            std::printf("  nucleo:");
            for (int id : ip.core)
                std::printf(" %s", dd.item(id) ? dd.item(id)->name.c_str() : "?");
            std::printf("\n  %s\n", ip.datasetNote.c_str());
            return 0;
        }
        if (cmd == "--meta") {
            // Daily local meta refresh (core/meta.h). Imports a larger batch of
            // the client history, then rebuilds the builds table from what is
            // already stored. Idempotent: safe to run again at any time.
            int n = argc > 2 ? std::atoi(argv[2]) : Config::load().metaImportCount;
            if (n <= 0) n = 60;
            int imported = 0;
            if (lcu.connected()) {
                auto r = importFromClient(db, lcu, ddOk ? &dd : nullptr, n);
                if (!r.error.empty())
                    std::printf("aviso: el cliente no dio todo el historial (%s)\n",
                                r.error.c_str());
                imported = r.imported;
            } else {
                std::printf("aviso: cliente cerrado; se reconstruye con lo ya guardado\n");
            }
            // Matches imported before the rune block existed carry no page.
            // Ask the client for them again before rebuilding.
            int refetched = refetchRunePages(db, lcu, ddOk ? &dd : nullptr);
            int rows = db.rebuildBuilds(ddOk ? &dd : nullptr);
            db.setKv("last_meta_refresh", util::todayLocal());
            db.audit("meta_refresh", json{{"imported", imported}, {"build_rows", rows}}.dump());
            std::printf("%d partidas nuevas, %d con runas recuperadas, %d builds en la muestra local.\n",
                        imported, refetched, rows);
            return 0;
        }
        if (cmd == "--analyze") {
            int n = analyzePending(db, ddOk ? &dd : nullptr);
            std::printf("%d partidas analizadas.\n", n);
            if (!db.pendingAnalysis().empty()) {
                std::printf("Quedan %zu pendientes. Causas posibles: falta el timeline en el "
                            "import, o no se pudo identificarte (usa --set-riot-id Nombre#TAG "
                            "igual que aparece en la partida).\n",
                            db.pendingAnalysis().size());
            }
            if (n > 0) {
                auto rows = db.listMatches(1);
                if (!rows.empty())
                    if (auto a = db.loadAnalysis(rows[0].matchId)) printAnalysis(*a);
            }
            return 0;
        }
        if (cmd == "--show" && args.size() >= 2) {
            if (auto a = db.loadAnalysis(args[1])) printAnalysis(*a);
            else std::printf("no hay analisis para %s\n", args[1].c_str());
            return 0;
        }
        if (cmd == "--report") { report(db); return 0; }

        usage();
        return 0;
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
}
