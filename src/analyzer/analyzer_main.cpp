// RiftLoop.Analyzer: post-match ingest + analysis CLI (PRD 14.2).
// Runs at low priority and never touches the League client.
#include "core/analysis.h"
#include "core/config.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/ingest.h"
#include "core/missions.h"
#include "core/serial.h"
#include "core/util.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace rl;
using nlohmann::json;

namespace {

void usage() {
    std::printf(
        "RiftLoop.Analyzer - ingesta y analisis local\n\n"
        "  --ingest <archivo.json>...   Importa partidas (match-v5 o {match,timeline})\n"
        "  --fetch [N]                  Descarga tus ultimas N partidas (necesita API key)\n"
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

        // Data Dragon is needed from here on.
        Ddragon dd;
        bool ddOk = dd.load(true);
        if (!ddOk) std::printf("aviso: Data Dragon no disponible (sin red y sin cache); "
                               "el analisis pierde D09 y validaciones de parche.\n");

        if (cmd == "--auto") {
            // Agent-invoked mode: fetch when a key exists, then analyze.
            if (!Config::loadApiKey().empty()) fetchMatches(db, 5);
            analyzePending(db, ddOk ? &dd : nullptr);
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
