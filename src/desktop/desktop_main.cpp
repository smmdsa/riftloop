// RiftLoop.Desktop: native Win32 panel (PRD 14.2; WinUI 3 queda para una
// iteracion posterior). Paginas: Perfil, Partidas, Post-match, Mision,
// Draft Lab, Parche y Ajustes. Escucha al Agent por IPC.
#include "core/analysis.h"
#include "core/config.h"
#include "core/contracts.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/ingest.h"
#include "core/ipc.h"
#include "core/lcu.h"
#include "core/lcu_history.h"
#include "core/missions.h"
#include "core/patchimpact.h"
#include "core/planner.h"
#include "core/recommend.h"
#include "core/serial.h"
#include "core/util.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace rl;
using nlohmann::json;

namespace {

// ------------------------------------------------------------------ control ids
enum : int {
    // tabs
    IDC_TABS = 1000,
    // profile page
    IDC_RIOTID = 1100, IDC_ROUTING, IDC_MODE_ESCALAR, IDC_MODE_APRENDER,
    IDC_ROLES, IDC_POOL_LIST, IDC_POOL_CHAMP, IDC_POOL_ROLE, IDC_POOL_TIER,
    IDC_POOL_ADD, IDC_POOL_DEL, IDC_SAVE_PROFILE, IDC_IMPORT_JSON,
    IDC_FETCH_API, IDC_ANALYZE, IDC_APIKEY, IDC_SAVE_APIKEY, IDC_PROFILE_STATUS,
    // matches page
    IDC_MATCH_LIST = 1200, IDC_MATCH_OPEN, IDC_MATCH_REFRESH,
    // postmatch page
    IDC_POST_TEXT = 1300, IDC_FB_OK, IDC_FB_PARTIAL, IDC_FB_WRONG, IDC_FB_CTX,
    IDC_ACCEPT_MISSION,
    // mission page
    IDC_MISSION_TEXT = 1400, IDC_SUGGEST_MISSION, IDC_SKILLS_LIST, IDC_MISSION_REFRESH,
    // draft page
    IDC_DRAFT_ROLE = 1500, IDC_DRAFT_ALLIES, IDC_DRAFT_ENEMIES, IDC_DRAFT_BANS,
    IDC_DRAFT_GO, IDC_DRAFT_CHAMP, IDC_DRAFT_PLAN, IDC_DRAFT_OUT, IDC_DRAFT_QUIZ,
    // patch page
    IDC_PATCH_TEXT = 1600, IDC_PATCH_REFRESH,
    // settings page
    IDC_SET_OVERLAY = 1700, IDC_SET_LCU, IDC_SET_LOCKFILE, IDC_SET_SAVE,
    IDC_SET_WIPE, IDC_SET_AUDIT, IDC_SET_TEXT, IDC_SET_CAPTURE,
    // quiz dialog buttons
    IDC_QUIZ_OPT0 = 1800, IDC_QUIZ_OPT1, IDC_QUIZ_OPT2, IDC_QUIZ_OPT3, IDC_QUIZ_TEXT,
};

constexpr UINT WM_APP_IPC = WM_APP + 10;
constexpr int kPages = 7;
const wchar_t* kPageNames[kPages] = {L"Perfil", L"Partidas", L"Post-match", L"Misión",
                                     L"Draft Lab", L"Parche", L"Ajustes"};

struct App {
    HWND hwnd = nullptr;
    HWND tabs = nullptr;
    HFONT font = nullptr;
    HFONT mono = nullptr;
    std::vector<HWND> pageControls[kPages];
    int currentPage = 0;

    std::unique_ptr<Db> db;
    Ddragon dd;
    bool ddOk = false;

    std::unique_ptr<ipc::Client> client;
    std::string agentState = "desconocido";

    std::string currentMatchId;      // match shown in Post-match

    // Quiz window
    HWND quizWnd = nullptr;
    std::vector<QuizQuestion> quiz;
    int quizIndex = 0;
    int quizCorrect = 0;

    // Pending IPC payloads (handed to the UI thread)
    std::mutex ipcMutex;
    std::vector<std::string> ipcQueue;
};

App* g = nullptr;

// ------------------------------------------------------------------- helpers

std::wstring w(const std::string& s) { return util::widen(s); }
std::string n(HWND ctl) {
    wchar_t buf[2048];
    GetWindowTextW(ctl, buf, 2048);
    return util::narrow(buf);
}
HWND ctl(int id) { return GetDlgItem(g->hwnd, id); }

HWND mk(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y,
        int cx, int cy, int id, DWORD exStyle = 0) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, x, y, cx, cy,
                             g->hwnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g->font, TRUE);
    g->pageControls[page].push_back(h);
    return h;
}

void setText(int id, const std::string& utf8) { SetWindowTextW(ctl(id), w(utf8).c_str()); }
void switchPage(int page);

std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        size_t a = part.find_first_not_of(" \t");
        size_t b = part.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(part.substr(a, b - a + 1));
    }
    return out;
}

// Resolve a champion name typed by the user against ddragon ids (fuzzy-ish).
std::string resolveChamp(const std::string& name) {
    if (!g->ddOk || name.empty()) return name;
    if (g->dd.champion(name)) return name;
    std::string lower = name;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    for (auto& id : g->dd.championIds()) {
        std::string idLower = id;
        for (auto& c : idLower) c = (char)tolower((unsigned char)c);
        if (idLower == lower) return id;
        const ChampInfo* ci = g->dd.champion(id);
        std::string dn = ci->name;
        for (auto& c : dn) c = (char)tolower((unsigned char)c);
        if (dn == lower) return id;
    }
    return name;                     // unknown; caller reports it
}

// --------------------------------------------------------------- text builders

std::string analysisText(const AnalysisResult& a, const MatchRow* row) {
    std::ostringstream o;
    if (row)
        o << row->userChampion << " " << row->userRole << " - "
          << (row->userWin ? "VICTORIA" : "DERROTA") << "  (" << row->durationSec / 60
          << " min, parche " << row->patch << ")\r\n\r\n";
    o << "FORTALEZA\r\n  " << a.strength << "\r\n\r\n";
    int shown = 0;
    for (auto& f : a.findings) {
        if (f.failures == 0) continue;
        if (shown++ == 0) o << "PATRON PRIORITARIO\r\n";
        else if (shown == 2) o << "\r\nOTROS PATRONES DETECTADOS\r\n";
        if (shown > 3) break;
        o << "[" << f.detectorId << "] " << f.title << "   (confianza " << f.confidence
          << ", " << f.failures << " de " << f.opportunities << " oportunidades falladas)\r\n";
        o << "  Por qué importa: " << f.whyItMatters << "\r\n";
        o << "  Qué probar: " << f.alternative << "\r\n";
        int evn = 0;
        for (auto& ev : f.evidence) {
            if (++evn > 3) break;    // cognitive budget (PRD 10.2)
            o << "  Evidencia [" << util::formatGameClock(ev.gameTimestampMs) << "] "
              << ev.observedFacts << "\r\n";
            o << "     Inferencia (" << ev.confidence << "): " << ev.inference << "\r\n";
            o << "     Exclusiones: " << ev.exclusionsChecked << "\r\n";
        }
    }
    if (shown == 0) o << "Sin patrones con fallos detectados en esta partida.\r\n";
    o << "\r\nLIMITACIONES\r\n  " << a.limitations << "\r\n";
    return o.str();
}

// ddragon id -> localized display name.
std::string champDisplay(const std::string& id) {
    if (g->ddOk)
        if (const ChampInfo* c = g->dd.champion(id)) return c->name;
    return id;
}

std::string top3Text(const Top3& t) {
    std::ostringstream o;
    if (!t.available) {
        o << "Sin recomendación posible:\r\n  " << t.unavailableReason << "\r\n";
        return o.str();
    }
    o << "TOP 3 (parche " << t.patch << ")\r\n";
    o << "Incertidumbre: " << t.uncertaintyReason << "\r\n\r\n";
    for (auto& c : t.cards) {
        o << "== " << c.label << ": " << champDisplay(c.champion) << " ==\r\n";
        for (auto& r : c.reasons) o << "  + " << r << "\r\n";
        o << "  Riesgo: " << c.risk << "\r\n";
        o << "  Experiencia: " << c.experience << "  |  Confianza: " << c.confidence << "\r\n\r\n";
    }
    return o.str();
}

std::string itemName(int id) {
    if (g->ddOk)
        if (const ItemInfo* it = g->dd.item(id)) return it->name;
    return "Item " + std::to_string(id);
}

std::string planText(const std::string& champ, const std::string& role, const RunePlan& rp,
                     const SpellPlan& sp, const ItemPlan& ip,
                     const std::vector<QuizQuestion>& quiz) {
    std::ostringstream o;
    o << "PLAN PREGAME: " << champDisplay(champ) << " " << role << "\r\n\r\n";
    if (g->ddOk && rp.main.perks.size() >= 9) {
        o << "RUNAS - " << g->dd.styleName(rp.main.primaryStyle) << " + "
          << g->dd.styleName(rp.main.subStyle) << " (confianza " << rp.confidence << ")\r\n";
        o << "  Piedra angular: " << g->dd.perkName(rp.main.perks[0]) << "\r\n";
        o << "  Principales:  " << g->dd.perkName(rp.main.perks[1]) << " | "
          << g->dd.perkName(rp.main.perks[2]) << " | " << g->dd.perkName(rp.main.perks[3])
          << "\r\n";
        o << "  Secundarias:  " << g->dd.perkName(rp.main.perks[4]) << " | "
          << g->dd.perkName(rp.main.perks[5]) << "\r\n";
        o << "  Fragmentos:   " << g->dd.shardName(rp.main.perks[6]) << " | "
          << g->dd.shardName(rp.main.perks[7]) << " | " << g->dd.shardName(rp.main.perks[8])
          << "\r\n";
    } else {
        o << "RUNAS - " << rp.main.name << " (confianza " << rp.confidence << ")\r\n";
        o << "  Estilos " << rp.main.primaryStyle << " + " << rp.main.subStyle << ", perks:";
        for (int p : rp.main.perks) o << " " << p;
        o << "\r\n";
    }
    for (auto& r : rp.main.reasons) o << "  - " << r << "\r\n";
    if (rp.situational && g->ddOk && rp.situational->perks.size() >= 6) {
        o << "  Alternativa: " << g->dd.styleName(rp.situational->subStyle)
          << " secundaria con " << g->dd.perkName(rp.situational->perks[4]) << " + "
          << g->dd.perkName(rp.situational->perks[5]);
        if (!rp.situational->reasons.empty()) o << " (" << rp.situational->reasons[0] << ")";
        o << "\r\n";
    }
    std::string sp1 = g->ddOk ? g->dd.summonerDisplay(sp.spells[0]) : sp.spells[0];
    std::string sp2 = g->ddOk ? g->dd.summonerDisplay(sp.spells[1]) : sp.spells[1];
    o << "\r\nHECHIZOS: " << sp1 << " + " << sp2 << "\r\n  " << sp.reason << "\r\n";
    o << "\r\nITEMS (confianza " << ip.confidence << ")\r\n";
    o << "  Inicio:";
    for (int id : ip.starting) o << " " << itemName(id);
    o << "\r\n";
    if (!ip.core.empty()) {
        o << "  Núcleo:";
        for (int id : ip.core) o << " " << itemName(id) << " |";
        o << "\r\n";
    }
    for (auto& b : ip.boots) {
        o << "  Botas - " << b.label << ":";
        for (int id : b.items) o << " " << itemName(id);
        o << "\r\n    " << b.condition << "\r\n";
    }
    for (auto& b : ip.branches) {
        o << "  " << b.label << ":";
        for (int id : b.items) o << " " << itemName(id);
        o << "\r\n    " << b.condition << "\r\n";
    }
    o << "  Nota de datos: " << ip.datasetNote << "\r\n";
    if (!quiz.empty()) {
        o << "\r\nQUIZ DE LOADING (" << quiz.size() << " preguntas listas)\r\n";
        for (auto& q : quiz) o << "  - " << q.text << "\r\n";
    }
    return o.str();
}

// ------------------------------------------------------------------ pages: fill

void refreshMatchList() {
    HWND lv = ctl(IDC_MATCH_LIST);
    ListView_DeleteAllItems(lv);
    int i = 0;
    for (auto& r : g->db->listMatches(50)) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i++;
        std::wstring id = w(r.matchId);
        item.pszText = id.data();
        ListView_InsertItem(lv, &item);
        auto set = [&](int col, const std::string& s) {
            std::wstring ws = w(s);
            ListView_SetItemText(lv, item.iItem, col, ws.data());
        };
        set(1, r.userChampion);
        set(2, r.userRole);
        set(3, r.userWin ? "WIN" : "LOSS");
        set(4, std::to_string(r.durationSec / 60) + " min");
        set(5, r.patch);
        set(6, r.analyzed ? "analizada" : "pendiente");
    }
}

void refreshMission() {
    std::ostringstream o;
    if (auto prog = activeMissionProgress(*g->db)) {
        auto& m = prog->mission;
        o << "MISIÓN ACTIVA: " << m.name << "\r\n\r\n";
        o << "Hipótesis: " << m.hypothesis << "\r\n";
        o << "Métrica: " << m.metric << "\r\n";
        o << "Aplica: " << m.appliesWhen << "\r\n";
        o << "No aplica: " << m.doesNotApply << "\r\n\r\n";
        o << "Progreso: " << prog->gamesTracked << "/" << m.blockSize << " partidas, "
          << prog->successes << " de " << prog->validOpportunities
          << " oportunidades ejecutadas (objetivo " << m.targetSuccesses << ")\r\n";
    } else {
        bool suggested = false;
        for (auto& m : g->db->listMissions()) {
            if (m.status == MissionStatus::Suggested && !suggested) {
                suggested = true;
                o << "MISIÓN SUGERIDA (pulsa 'Aceptar sugerida'): " << m.name << "\r\n";
                o << "  Métrica: " << m.metric << "\r\n\r\n";
            }
            if (m.status == MissionStatus::Evaluated) {
                o << "Evaluada: " << m.name << " -> " << toString(m.result) << "\r\n";
            }
        }
        if (!suggested) o << "Sin misión activa. Analiza partidas y pulsa 'Sugerir misión'.\r\n";
    }
    o << "\r\nRacha de mejora: " << g->db->streakDays() << " día(s)   XP: " << g->db->totalXp()
      << "\r\n";
    setText(IDC_MISSION_TEXT, o.str());

    HWND lv = ctl(IDC_SKILLS_LIST);
    ListView_DeleteAllItems(lv);
    int i = 0;
    for (auto& s : g->db->loadSkills()) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i++;
        std::wstring dom = w(s.domain);
        item.pszText = dom.data();
        ListView_InsertItem(lv, &item);
        std::wstring st = w(toString(s.state));
        ListView_SetItemText(lv, item.iItem, 1, st.data());
        std::wstring conf = w(s.confidence + " (" + std::to_string(s.opportunitiesSeen) + " oport.)");
        ListView_SetItemText(lv, item.iItem, 2, conf.data());
    }
}

void showAnalysis(const std::string& matchId) {
    auto a = g->db->loadAnalysis(matchId);
    if (!a) {
        setText(IDC_POST_TEXT, "No hay análisis para " + matchId +
                               ". Usa 'Analizar pendientes' en Perfil.");
        return;
    }
    g->currentMatchId = matchId;
    MatchRow rowCopy;
    const MatchRow* rowPtr = nullptr;
    for (auto& r : g->db->listMatches(100))
        if (r.matchId == matchId) { rowCopy = r; rowPtr = &rowCopy; break; }
    setText(IDC_POST_TEXT, analysisText(*a, rowPtr));
    TabCtrl_SetCurSel(g->tabs, 2);
    switchPage(2);
}

void refreshProfileUi() {
    Profile p = g->db->loadProfile();
    setText(IDC_RIOTID, p.riotId);
    CheckDlgButton(g->hwnd, IDC_MODE_ESCALAR, p.mode == AppMode::Escalar ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_MODE_APRENDER, p.mode == AppMode::Aprender ? BST_CHECKED : 0);
    std::string roles;
    for (auto& r : p.preferredRoles) roles += (roles.empty() ? "" : ", ") + r;
    setText(IDC_ROLES, roles);

    HWND lv = ctl(IDC_POOL_LIST);
    ListView_DeleteAllItems(lv);
    int i = 0;
    for (auto& e : p.pool) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i++;
        std::wstring champ = w(e.champion);
        item.pszText = champ.data();
        ListView_InsertItem(lv, &item);
        std::wstring role = w(e.role);
        ListView_SetItemText(lv, item.iItem, 1, role.data());
        const char* tier = e.tier == PoolTier::Main ? "main"
                         : e.tier == PoolTier::Comfort ? "cómodo"
                         : e.tier == PoolTier::Learning ? "aprendiendo" : "no recomendar";
        std::wstring wt = w(tier);
        ListView_SetItemText(lv, item.iItem, 2, wt.data());
        std::wstring games = w(std::to_string(e.declaredGames));
        ListView_SetItemText(lv, item.iItem, 3, games.data());
    }
}

// ------------------------------------------------------------------- actions

void saveProfileFromUi() {
    Profile p = g->db->loadProfile();
    p.riotId = n(ctl(IDC_RIOTID));
    p.mode = IsDlgButtonChecked(g->hwnd, IDC_MODE_APRENDER) ? AppMode::Aprender : AppMode::Escalar;
    p.preferredRoles = splitCsv(n(ctl(IDC_ROLES)));
    g->db->saveProfile(p);
    setText(IDC_PROFILE_STATUS, "Perfil guardado.");
}

void addPoolEntry() {
    std::string champ = resolveChamp(n(ctl(IDC_POOL_CHAMP)));
    if (champ.empty()) return;
    if (g->ddOk && !g->dd.champion(champ)) {
        setText(IDC_PROFILE_STATUS, "Campeón desconocido en el parche " + g->dd.version() + ": " +
                                    champ);
        return;
    }
    Profile p = g->db->loadProfile();
    PoolEntry e;
    e.champion = champ;
    int roleSel = (int)SendMessageW(ctl(IDC_POOL_ROLE), CB_GETCURSEL, 0, 0);
    const char* roles[] = {"TOP", "JUNGLE", "MIDDLE", "BOTTOM", "UTILITY"};
    e.role = roleSel >= 0 && roleSel < 5 ? roles[roleSel] : "MIDDLE";
    int tierSel = (int)SendMessageW(ctl(IDC_POOL_TIER), CB_GETCURSEL, 0, 0);
    e.tier = tierSel == 0 ? PoolTier::Main
           : tierSel == 2 ? PoolTier::Learning
           : tierSel == 3 ? PoolTier::DoNotRecommend : PoolTier::Comfort;
    p.pool.push_back(e);
    g->db->saveProfile(p);
    refreshProfileUi();
    setText(IDC_POOL_CHAMP, "");
    setText(IDC_PROFILE_STATUS, champ + " añadido al pool.");
}

void removePoolEntry() {
    HWND lv = ctl(IDC_POOL_LIST);
    int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    if (sel < 0) return;
    Profile p = g->db->loadProfile();
    if (sel < (int)p.pool.size()) {
        p.pool.erase(p.pool.begin() + sel);
        g->db->saveProfile(p);
        refreshProfileUi();
    }
}

void importJsonFiles() {
    wchar_t files[8192] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g->hwnd;
    ofn.lpstrFilter = L"JSON\0*.json\0";
    ofn.lpstrFile = files;
    ofn.nMaxFile = 8192;
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    // Multi-select: dir\0file1\0file2\0\0 or a single full path.
    std::vector<std::wstring> paths;
    std::wstring dir = files;
    wchar_t* p2 = files + dir.size() + 1;
    if (*p2 == 0) {
        paths.push_back(dir);
    } else {
        while (*p2) {
            std::wstring f = p2;
            paths.push_back(dir + L"\\" + f);
            p2 += f.size() + 1;
        }
    }
    int ok = 0, skip = 0;
    for (auto& path : paths) {
        std::string text = util::readFile(path);
        auto pair = splitImport(text);
        if (!pair) { ++skip; continue; }
        auto m = parseMatch(pair->matchJson);
        if (m && g->db->upsertMatch(*m, pair->matchJson, pair->timelineJson)) ++ok;
        else ++skip;
    }
    setText(IDC_PROFILE_STATUS, std::to_string(ok) + " partidas importadas, " +
                                std::to_string(skip) + " omitidas. Pulsa 'Analizar pendientes'.");
    refreshMatchList();
}

void runAnalysis() {
    setText(IDC_PROFILE_STATUS, "Analizando...");
    EnableWindow(ctl(IDC_ANALYZE), FALSE);
    std::thread([] {
        int done = 0;
        try {
            Db db;                   // separate connection for the worker
            done = analyzePending(db, g->ddOk ? &g->dd : nullptr);
        } catch (...) {}
        PostMessageW(g->hwnd, WM_APP_IPC, 1, (LPARAM)done);
    }).detach();
}

void runFetch() {
    // Match history straight from the League client: no API key (user request).
    setText(IDC_PROFILE_STATUS, "Descargando historial desde el cliente de League...");
    EnableWindow(ctl(IDC_FETCH_API), FALSE);
    std::thread([] {
        int imported = -1;
        try {
            Db db;               // worker connection
            Config cfg = Config::load();
            Lcu lcu;
            if (lcu.connect(cfg.leagueLockfilePath)) {
                auto r = importFromClient(db, lcu, g->ddOk ? &g->dd : nullptr,
                                          cfg.matchImportCount);
                imported = r.error.empty() ? r.imported : -1;
            }
        } catch (...) {}
        PostMessageW(g->hwnd, WM_APP_IPC, 2, (LPARAM)imported);
    }).detach();
}

void draftRecommend() {
    DraftContext ctx;
    int roleSel = (int)SendMessageW(ctl(IDC_DRAFT_ROLE), CB_GETCURSEL, 0, 0);
    const char* roles[] = {"TOP", "JUNGLE", "MIDDLE", "BOTTOM", "UTILITY"};
    ctx.role = roleSel >= 0 && roleSel < 5 ? roles[roleSel] : "MIDDLE";
    std::string unknown;
    auto resolveList = [&](const std::string& csv) {
        std::vector<std::string> out;
        for (auto& name : splitCsv(csv)) {
            std::string id = resolveChamp(name);
            if (g->ddOk && !g->dd.champion(id)) unknown += (unknown.empty() ? "" : ", ") + name;
            else out.push_back(id);
        }
        return out;
    };
    ctx.allyChampions = resolveList(n(ctl(IDC_DRAFT_ALLIES)));
    ctx.enemyChampions = resolveList(n(ctl(IDC_DRAFT_ENEMIES)));
    ctx.bans = resolveList(n(ctl(IDC_DRAFT_BANS)));
    ctx.patch = g->ddOk ? g->dd.version() : "";

    if (!g->ddOk) {
        setText(IDC_DRAFT_OUT, "Data Dragon no disponible: sin datos estáticos no se recomienda.");
        return;
    }
    Top3 top = recommendTop3(*g->db, g->dd, ctx);
    json contract = makeContract("top3", ctx.patch, json{{"role", ctx.role}}, json(top),
                                 top.cards.empty() ? "baja" : top.cards[0].confidence,
                                 top.uncertaintyReason);
    g->db->saveRecommendation("top3", contract.dump());
    std::string out = top3Text(top);
    if (!unknown.empty()) out += "\r\nNombres no reconocidos: " + unknown + "\r\n";
    setText(IDC_DRAFT_OUT, out);
}

void draftPlan() {
    if (!g->ddOk) {
        setText(IDC_DRAFT_OUT, "Data Dragon no disponible.");
        return;
    }
    std::string champ = resolveChamp(n(ctl(IDC_DRAFT_CHAMP)));
    if (champ.empty() || !g->dd.champion(champ)) {
        setText(IDC_DRAFT_OUT, "Escribe un campeón válido en 'Tu campeón'.");
        return;
    }
    PlanInput pi;
    pi.champion = champ;
    int roleSel = (int)SendMessageW(ctl(IDC_DRAFT_ROLE), CB_GETCURSEL, 0, 0);
    const char* roles[] = {"TOP", "JUNGLE", "MIDDLE", "BOTTOM", "UTILITY"};
    pi.role = roleSel >= 0 && roleSel < 5 ? roles[roleSel] : "MIDDLE";
    for (auto& name : splitCsv(n(ctl(IDC_DRAFT_ALLIES)))) pi.draft.allyChampions.push_back(resolveChamp(name));
    for (auto& name : splitCsv(n(ctl(IDC_DRAFT_ENEMIES)))) pi.draft.enemyChampions.push_back(resolveChamp(name));
    pi.draft.role = pi.role;

    RunePlan rp = planRunes(g->dd, pi);
    SpellPlan sp = planSpells(g->dd, pi);
    ItemPlan ip = planItems(*g->db, g->dd, pi);
    g->quiz = buildQuiz(g->dd, pi);

    json payload;
    payload["champion"] = pi.champion;
    payload["role"] = pi.role;
    payload["runes"] = rp;
    payload["spells"] = sp;
    payload["items"] = ip;
    json contract = makeContract("pregame_plan", g->dd.version(),
                                 json{{"champion", pi.champion}, {"role", pi.role}}, payload,
                                 ip.confidence, "plan manual desde Draft Lab");
    g->db->saveRecommendation("pregame_plan", contract.dump());

    setText(IDC_DRAFT_OUT, planText(pi.champion, pi.role, rp, sp, ip, g->quiz));
}

void giveFeedback(const char* label) {
    if (g->currentMatchId.empty()) return;
    auto a = g->db->loadAnalysis(g->currentMatchId);
    if (!a) return;
    std::string detector = "-";
    for (auto& f : a->findings)
        if (f.failures > 0) { detector = f.detectorId; break; }
    g->db->saveFeedback(g->currentMatchId, detector, label, "");
    g->db->recordActivity(util::todayLocal(), "evidence_reviewed");
    g->db->addXp(5, "feedback de diagnóstico");
    setText(IDC_PROFILE_STATUS, "Feedback registrado: " + std::string(label));
    MessageBoxW(g->hwnd, L"Feedback registrado. Se usa para calibrar, no reentrena modelos.",
                L"RiftLoop", MB_OK | MB_ICONINFORMATION);
}

void acceptSuggestedMission() {
    for (auto& m : g->db->listMissions()) {
        if (m.status == MissionStatus::Suggested) {
            activateMission(*g->db, m.dbId);
            refreshMission();
            MessageBoxW(g->hwnd, L"Misión activada. Se medirá en tus próximas 3-5 partidas.",
                        L"RiftLoop", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    MessageBoxW(g->hwnd, L"No hay misión sugerida. Pulsa 'Sugerir misión' primero.", L"RiftLoop",
                MB_OK | MB_ICONINFORMATION);
}

void refreshPatch() {
    if (!g->ddOk) {
        setText(IDC_PATCH_TEXT, "Data Dragon no disponible.");
        return;
    }
    auto rep = patchImpact(*g->db, g->dd);
    std::ostringstream o;
    o << "IMPACTO DE PARCHE  " << (rep.fromVersion.empty() ? "(sin snapshot anterior)" :
                                    rep.fromVersion + " -> " + rep.toVersion)
      << "\r\nVersión de datos actual: " << rep.toVersion << "\r\n\r\n";
    for (auto& e : rep.entries)
        o << (e.direct ? "[CAMBIO] " : "[  ok  ] ") << e.champion << ": " << e.change << "\r\n";
    o << "\r\n" << rep.note << "\r\n";
    setText(IDC_PATCH_TEXT, o.str());
}

void refreshSettings() {
    Config cfg = Config::load();
    CheckDlgButton(g->hwnd, IDC_SET_OVERLAY, cfg.overlayEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_LCU, cfg.lcuReadEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_CAPTURE, cfg.captureEnabled ? BST_CHECKED : 0);
    setText(IDC_SET_LOCKFILE, cfg.leagueLockfilePath);
    std::ostringstream o;
    o << "Datos locales: " << util::narrow(util::dataDir().wstring()) << "\r\n";
    o << "Estado del agente/cliente: " << g->agentState << "\r\n";
    o << "Data Dragon: " << (g->ddOk ? g->dd.version() : "no disponible") << "\r\n";
    o << "Grabación: fuera de la iteración 1 (flag apagado).\r\n";
    o << "Escrituras al cliente de League: no existen en este build (read-only, PRD 17).\r\n";
    setText(IDC_SET_TEXT, o.str());
}

void showAudit() {
    std::ostringstream o;
    for (auto& line : g->db->auditLog(60)) o << line << "\r\n";
    if (o.str().empty()) o << "(log vacío)";
    setText(IDC_SET_TEXT, o.str());
}

// ------------------------------------------------------------------- quiz ui

void quizShowQuestion();

LRESULT CALLBACK QuizProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id >= IDC_QUIZ_OPT0 && id <= IDC_QUIZ_OPT3) {
            int chosen = id - IDC_QUIZ_OPT0;
            auto& q = g->quiz[g->quizIndex];
            bool right = chosen == q.correctIndex;
            if (right) ++g->quizCorrect;
            std::wstring text = right ? L"Correcto.\n\n" : L"No exactamente.\n\n";
            text += w(q.explanation);
            MessageBoxW(hwnd, text.c_str(), right ? L"✔" : L"✖", MB_OK | MB_SETFOREGROUND);
            ++g->quizIndex;
            if (g->quizIndex >= (int)g->quiz.size()) {
                try {
                    g->db->recordActivity(util::todayLocal(), "quiz_completed");
                    g->db->addXp(10 + 5 * g->quizCorrect, "quiz de loading");
                } catch (...) {}
                DestroyWindow(hwnd);
            } else {
                quizShowQuestion();
            }
            return 0;
        }
    }
    if (msg == WM_CLOSE) {           // closing never punishes the streak (RF-QUIZ-003)
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        g->quizWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void quizShowQuestion() {
    if (!g->quizWnd) return;
    auto& q = g->quiz[g->quizIndex];
    SetWindowTextW(GetDlgItem(g->quizWnd, IDC_QUIZ_TEXT),
                   (w("Pregunta " + std::to_string(g->quizIndex + 1) + " de " +
                      std::to_string(g->quiz.size()) + "\n\n" + q.text)).c_str());
    for (int i = 0; i < 4; ++i) {
        HWND b = GetDlgItem(g->quizWnd, IDC_QUIZ_OPT0 + i);
        if (i < (int)q.options.size()) {
            SetWindowTextW(b, w(q.options[i]).c_str());
            ShowWindow(b, SW_SHOW);
        } else {
            ShowWindow(b, SW_HIDE);
        }
    }
}

void openQuizWindow() {
    if (g->quiz.empty()) {
        try {
            std::string stored = g->db->lastRecommendation("quiz");
            if (!stored.empty()) g->quiz = json::parse(stored).get<std::vector<QuizQuestion>>();
        } catch (...) {}
    }
    if (g->quiz.empty() || g->quizWnd) return;
    g->quizIndex = 0;
    g->quizCorrect = 0;

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = QuizProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"RiftLoopQuizWnd";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    int sw = GetSystemMetrics(SM_CXSCREEN);
    g->quizWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"RiftLoopQuizWnd",
                                 L"RiftLoop - Quiz de loading",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 sw - 460, 80, 420, 330, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    HWND st = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 12, 380, 110,
                            g->quizWnd, (HMENU)IDC_QUIZ_TEXT, nullptr, nullptr);
    SendMessageW(st, WM_SETFONT, (WPARAM)g->font, TRUE);
    for (int i = 0; i < 4; ++i) {
        HWND b = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 16,
                               130 + i * 40, 380, 34, g->quizWnd,
                               (HMENU)(INT_PTR)(IDC_QUIZ_OPT0 + i), nullptr, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)g->font, TRUE);
    }
    quizShowQuestion();
    ShowWindow(g->quizWnd, SW_SHOWNOACTIVATE);   // never steals focus from the game
}

// ------------------------------------------------------------------ ipc glue

void onIpc(const std::string& text) {
    {
        std::lock_guard lk(g->ipcMutex);
        g->ipcQueue.push_back(text);
    }
    PostMessageW(g->hwnd, WM_APP_IPC, 0, 0);
}

void processIpcQueue() {
    std::vector<std::string> pending;
    {
        std::lock_guard lk(g->ipcMutex);
        pending.swap(g->ipcQueue);
    }
    for (auto& text : pending) {
        try {
            json j = json::parse(text);
            std::string type = j.value("type", "");
            if (type == "state") {
                g->agentState = j.value("state", "?");
                SetWindowTextW(g->hwnd, w("RiftLoop - " + g->agentState).c_str());
            } else if (type == "top3") {
                Top3 top = j.at("data").at("options").get<Top3>();
                setText(IDC_DRAFT_OUT, "[EN VIVO desde champion select]\r\n\r\n" + top3Text(top));
            } else if (type == "plan") {
                const json& d = j.at("data").at("options");
                RunePlan rp = d.at("runes").get<RunePlan>();
                SpellPlan sp = d.at("spells").get<SpellPlan>();
                ItemPlan ip = d.at("items").get<ItemPlan>();
                g->quiz = j.value("quiz", std::vector<QuizQuestion>{});
                setText(IDC_DRAFT_OUT, "[EN VIVO: pick bloqueado]\r\n\r\n" +
                        planText(d.value("champion", ""), d.value("role", ""), rp, sp, ip, g->quiz));
            } else if (type == "quiz_show") {
                openQuizWindow();
            } else if (type == "quiz_hide") {
                if (g->quizWnd) DestroyWindow(g->quizWnd);
            } else if (type == "analysis_ready") {
                refreshMatchList();
                auto rows = g->db->listMatches(1);
                if (!rows.empty() && rows[0].analyzed) showAnalysis(rows[0].matchId);
                refreshMission();
            }
        } catch (...) {}
    }
}

// -------------------------------------------------------------- page building

void addListViewColumns(HWND lv, const std::vector<std::pair<const wchar_t*, int>>& cols) {
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT);
    int i = 0;
    for (auto& [name, width] : cols) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(name);
        col.cx = width;
        ListView_InsertColumn(lv, i++, &col);
    }
}

void buildPages() {
    // ---- page 0: Perfil ----------------------------------------------------
    mk(0, L"STATIC", L"Riot ID (Nombre#TAG):", WS_VISIBLE, 20, 50, 150, 22, 0);
    mk(0, L"EDIT", L"", WS_VISIBLE | WS_BORDER, 180, 48, 200, 24, IDC_RIOTID);
    mk(0, L"BUTTON", L"Modo Escalar", WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 400, 48, 120, 24,
       IDC_MODE_ESCALAR);
    mk(0, L"BUTTON", L"Modo Aprender", WS_VISIBLE | BS_AUTORADIOBUTTON, 530, 48, 130, 24,
       IDC_MODE_APRENDER);
    mk(0, L"STATIC", L"Roles preferidos (CSV):", WS_VISIBLE, 20, 82, 150, 22, 0);
    mk(0, L"EDIT", L"MIDDLE", WS_VISIBLE | WS_BORDER, 180, 80, 200, 24, IDC_ROLES);
    mk(0, L"BUTTON", L"Guardar perfil", WS_VISIBLE | BS_PUSHBUTTON, 400, 79, 130, 26,
       IDC_SAVE_PROFILE);

    mk(0, L"STATIC", L"Champion pool:", WS_VISIBLE, 20, 118, 150, 22, 0);
    HWND lv = mk(0, WC_LISTVIEWW, L"", WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 20,
                 142, 500, 190, IDC_POOL_LIST);
    addListViewColumns(lv, {{L"Campeón", 150}, {L"Rol", 90}, {L"Nivel", 120}, {L"Partidas", 80}});
    mk(0, L"EDIT", L"", WS_VISIBLE | WS_BORDER, 540, 142, 150, 24, IDC_POOL_CHAMP);
    HWND cbRole = mk(0, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 540, 172,
                     150, 160, IDC_POOL_ROLE);
    for (auto* r : {L"TOP", L"JUNGLE", L"MIDDLE", L"BOTTOM", L"UTILITY"})
        SendMessageW(cbRole, CB_ADDSTRING, 0, (LPARAM)r);
    SendMessageW(cbRole, CB_SETCURSEL, 2, 0);
    HWND cbTier = mk(0, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 540, 202,
                     150, 160, IDC_POOL_TIER);
    for (auto* t : {L"main", L"cómodo", L"aprendiendo", L"no recomendar"})
        SendMessageW(cbTier, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(cbTier, CB_SETCURSEL, 1, 0);
    mk(0, L"BUTTON", L"Añadir", WS_VISIBLE | BS_PUSHBUTTON, 540, 232, 150, 26, IDC_POOL_ADD);
    mk(0, L"BUTTON", L"Quitar seleccionado", WS_VISIBLE | BS_PUSHBUTTON, 540, 262, 150, 26,
       IDC_POOL_DEL);

    mk(0, L"STATIC", L"Datos:", WS_VISIBLE, 20, 348, 60, 22, 0);
    mk(0, L"BUTTON", L"Importar partidas (JSON)...", WS_VISIBLE | BS_PUSHBUTTON, 90, 344, 200, 28,
       IDC_IMPORT_JSON);
    mk(0, L"BUTTON", L"Descargar del cliente (League)", WS_VISIBLE | BS_PUSHBUTTON, 300, 344, 200,
       28, IDC_FETCH_API);
    mk(0, L"BUTTON", L"Analizar pendientes", WS_VISIBLE | BS_PUSHBUTTON, 510, 344, 170, 28,
       IDC_ANALYZE);
    mk(0, L"STATIC",
       L"Con el cliente de League abierto, tu identidad e historial se leen del propio cliente. "
       L"No se necesita API key.",
       WS_VISIBLE, 20, 386, 700, 34, 0);
    mk(0, L"STATIC", L"", WS_VISIBLE, 20, 424, 700, 44, IDC_PROFILE_STATUS);

    // ---- page 1: Partidas --------------------------------------------------
    HWND ml = mk(1, WC_LISTVIEWW, L"", WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 20, 50,
                 860, 420, IDC_MATCH_LIST);
    addListViewColumns(ml, {{L"Match ID", 180}, {L"Campeón", 110}, {L"Rol", 90}, {L"Resultado", 90},
                            {L"Duración", 80}, {L"Parche", 80}, {L"Estado", 100}});
    mk(1, L"BUTTON", L"Ver análisis", WS_VISIBLE | BS_PUSHBUTTON, 20, 480, 140, 28, IDC_MATCH_OPEN);
    mk(1, L"BUTTON", L"Refrescar", WS_VISIBLE | BS_PUSHBUTTON, 170, 480, 120, 28,
       IDC_MATCH_REFRESH);

    // ---- page 2: Post-match ------------------------------------------------
    HWND pt = mk(2, L"EDIT", L"Selecciona una partida analizada en la pestaña Partidas.",
                 WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20, 50, 860,
                 380, IDC_POST_TEXT);
    SendMessageW(pt, WM_SETFONT, (WPARAM)g->mono, TRUE);
    mk(2, L"STATIC", L"¿El diagnóstico principal es correcto?", WS_VISIBLE, 20, 442, 250, 22, 0);
    mk(2, L"BUTTON", L"Correcto", WS_VISIBLE | BS_PUSHBUTTON, 270, 438, 100, 28, IDC_FB_OK);
    mk(2, L"BUTTON", L"Parcial", WS_VISIBLE | BS_PUSHBUTTON, 375, 438, 100, 28, IDC_FB_PARTIAL);
    mk(2, L"BUTTON", L"Incorrecto", WS_VISIBLE | BS_PUSHBUTTON, 480, 438, 100, 28, IDC_FB_WRONG);
    mk(2, L"BUTTON", L"Falta contexto", WS_VISIBLE | BS_PUSHBUTTON, 585, 438, 120, 28, IDC_FB_CTX);
    mk(2, L"BUTTON", L"Aceptar misión sugerida", WS_VISIBLE | BS_PUSHBUTTON, 20, 478, 200, 30,
       IDC_ACCEPT_MISSION);

    // ---- page 3: Misión ----------------------------------------------------
    HWND mt = mk(3, L"EDIT", L"", WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                 20, 50, 500, 300, IDC_MISSION_TEXT);
    SendMessageW(mt, WM_SETFONT, (WPARAM)g->mono, TRUE);
    mk(3, L"BUTTON", L"Sugerir misión", WS_VISIBLE | BS_PUSHBUTTON, 20, 360, 150, 30,
       IDC_SUGGEST_MISSION);
    mk(3, L"BUTTON", L"Refrescar", WS_VISIBLE | BS_PUSHBUTTON, 180, 360, 120, 30,
       IDC_MISSION_REFRESH);
    mk(3, L"STATIC", L"Árbol privado de habilidades:", WS_VISIBLE, 540, 50, 250, 22, 0);
    HWND sl = mk(3, WC_LISTVIEWW, L"", WS_VISIBLE | WS_BORDER | LVS_REPORT, 540, 74, 340, 320,
                 IDC_SKILLS_LIST);
    addListViewColumns(sl, {{L"Dominio", 130}, {L"Estado", 100}, {L"Confianza", 100}});

    // ---- page 4: Draft Lab -------------------------------------------------
    mk(4, L"STATIC", L"Rol:", WS_VISIBLE, 20, 52, 40, 22, 0);
    HWND dr = mk(4, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST, 60, 48, 120, 160,
                 IDC_DRAFT_ROLE);
    for (auto* r : {L"TOP", L"JUNGLE", L"MIDDLE", L"BOTTOM", L"UTILITY"})
        SendMessageW(dr, CB_ADDSTRING, 0, (LPARAM)r);
    SendMessageW(dr, CB_SETCURSEL, 2, 0);
    mk(4, L"STATIC", L"Aliados (CSV):", WS_VISIBLE, 200, 52, 95, 22, 0);
    mk(4, L"EDIT", L"", WS_VISIBLE | WS_BORDER, 295, 48, 250, 24, IDC_DRAFT_ALLIES);
    mk(4, L"STATIC", L"Rivales (CSV):", WS_VISIBLE, 560, 52, 95, 22, 0);
    mk(4, L"EDIT", L"", WS_VISIBLE | WS_BORDER, 655, 48, 225, 24, IDC_DRAFT_ENEMIES);
    mk(4, L"STATIC", L"Bans (CSV):", WS_VISIBLE, 20, 84, 80, 22, 0);
    mk(4, L"EDIT", L"", WS_VISIBLE | WS_BORDER, 100, 82, 200, 24, IDC_DRAFT_BANS);
    mk(4, L"BUTTON", L"Recomendar Top 3", WS_VISIBLE | BS_PUSHBUTTON, 320, 80, 150, 28,
       IDC_DRAFT_GO);
    mk(4, L"STATIC", L"Tu campeón:", WS_VISIBLE, 490, 84, 85, 22, 0);
    mk(4, L"EDIT", L"", WS_VISIBLE | WS_BORDER, 575, 82, 130, 24, IDC_DRAFT_CHAMP);
    mk(4, L"BUTTON", L"Plan pregame", WS_VISIBLE | BS_PUSHBUTTON, 715, 80, 120, 28,
       IDC_DRAFT_PLAN);
    mk(4, L"BUTTON", L"Probar quiz", WS_VISIBLE | BS_PUSHBUTTON, 20, 480, 120, 28, IDC_DRAFT_QUIZ);
    HWND dout = mk(4, L"EDIT", L"En champion select, esta pestaña se rellena sola (Agent).\r\n"
                    L"Sin cliente de League: escribe la composición y pulsa Recomendar.",
                    WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20, 118, 860,
                    350, IDC_DRAFT_OUT);
    SendMessageW(dout, WM_SETFONT, (WPARAM)g->mono, TRUE);

    // ---- page 5: Parche ----------------------------------------------------
    HWND ptx = mk(5, L"EDIT", L"", WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                  20, 50, 860, 400, IDC_PATCH_TEXT);
    SendMessageW(ptx, WM_SETFONT, (WPARAM)g->mono, TRUE);
    mk(5, L"BUTTON", L"Actualizar", WS_VISIBLE | BS_PUSHBUTTON, 20, 460, 120, 28,
       IDC_PATCH_REFRESH);

    // ---- page 6: Ajustes ---------------------------------------------------
    mk(6, L"BUTTON", L"Overlay en partida", WS_VISIBLE | BS_AUTOCHECKBOX, 20, 50, 200, 24,
       IDC_SET_OVERLAY);
    mk(6, L"BUTTON", L"Lectura del cliente (LCU) activa", WS_VISIBLE | BS_AUTOCHECKBOX, 20, 80,
       260, 24, IDC_SET_LCU);
    mk(6, L"BUTTON", L"Grabación local en partida (beta, solo ventana del juego)",
       WS_VISIBLE | BS_AUTOCHECKBOX, 300, 50, 420, 24, IDC_SET_CAPTURE);
    mk(6, L"STATIC", L"Ruta del lockfile (vacío = autodetectar):", WS_VISIBLE, 20, 114, 260, 22, 0);
    mk(6, L"EDIT", L"", WS_VISIBLE | WS_BORDER, 280, 112, 400, 24, IDC_SET_LOCKFILE);
    mk(6, L"BUTTON", L"Guardar ajustes", WS_VISIBLE | BS_PUSHBUTTON, 20, 148, 150, 28,
       IDC_SET_SAVE);
    mk(6, L"BUTTON", L"Ver log de auditoría", WS_VISIBLE | BS_PUSHBUTTON, 180, 148, 160, 28,
       IDC_SET_AUDIT);
    mk(6, L"BUTTON", L"Borrar TODOS los datos", WS_VISIBLE | BS_PUSHBUTTON, 350, 148, 190, 28,
       IDC_SET_WIPE);
    HWND stx = mk(6, L"EDIT", L"", WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                  20, 190, 860, 280, IDC_SET_TEXT);
    SendMessageW(stx, WM_SETFONT, (WPARAM)g->mono, TRUE);
}

void switchPage(int page) {
    g->currentPage = page;
    for (int p = 0; p < kPages; ++p)
        for (HWND h : g->pageControls[p])
            ShowWindow(h, p == page ? SW_SHOW : SW_HIDE);
    switch (page) {
        case 0: refreshProfileUi(); break;
        case 1: refreshMatchList(); break;
        case 3: refreshMission(); break;
        case 5: refreshPatch(); break;
        case 6: refreshSettings(); break;
    }
}

// --------------------------------------------------------------------- wndproc

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lp;
            if (hdr->idFrom == IDC_TABS && hdr->code == TCN_SELCHANGE) {
                switchPage(TabCtrl_GetCurSel(g->tabs));
                return 0;
            }
            if (hdr->idFrom == IDC_MATCH_LIST && hdr->code == NM_DBLCLK) {
                int sel = ListView_GetNextItem(ctl(IDC_MATCH_LIST), -1, LVNI_SELECTED);
                if (sel >= 0) {
                    wchar_t buf[64];
                    ListView_GetItemText(ctl(IDC_MATCH_LIST), sel, 0, buf, 64);
                    showAnalysis(util::narrow(buf));
                    TabCtrl_SetCurSel(g->tabs, 2);
                    switchPage(2);
                }
                return 0;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SAVE_PROFILE: saveProfileFromUi(); break;
                case IDC_POOL_ADD: addPoolEntry(); break;
                case IDC_POOL_DEL: removePoolEntry(); break;
                case IDC_IMPORT_JSON: importJsonFiles(); break;
                case IDC_FETCH_API: runFetch(); break;
                case IDC_ANALYZE: runAnalysis(); break;
                case IDC_MATCH_REFRESH: refreshMatchList(); break;
                case IDC_MATCH_OPEN: {
                    int sel = ListView_GetNextItem(ctl(IDC_MATCH_LIST), -1, LVNI_SELECTED);
                    if (sel >= 0) {
                        wchar_t buf[64];
                        ListView_GetItemText(ctl(IDC_MATCH_LIST), sel, 0, buf, 64);
                        showAnalysis(util::narrow(buf));
                        TabCtrl_SetCurSel(g->tabs, 2);
                        switchPage(2);
                    }
                    break;
                }
                case IDC_FB_OK: giveFeedback("correcto"); break;
                case IDC_FB_PARTIAL: giveFeedback("parcial"); break;
                case IDC_FB_WRONG: giveFeedback("incorrecto"); break;
                case IDC_FB_CTX: giveFeedback("falta_contexto"); break;
                case IDC_ACCEPT_MISSION: acceptSuggestedMission(); break;
                case IDC_SUGGEST_MISSION: {
                    if (auto m = suggestMission(*g->db)) {
                        refreshMission();
                        MessageBoxW(hwnd,
                                    w("Misión sugerida: " + m->name + "\n\nMétrica: " + m->metric +
                                      "\n\nPulsa 'Aceptar misión sugerida' en Post-match o "
                                      "'Refrescar' aquí.").c_str(),
                                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hwnd,
                                    L"Aún no hay un patrón con oportunidades suficientes "
                                    L"(se necesitan varias partidas analizadas).",
                                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }
                case IDC_MISSION_REFRESH: refreshMission(); break;
                case IDC_DRAFT_GO: draftRecommend(); break;
                case IDC_DRAFT_PLAN: draftPlan(); break;
                case IDC_DRAFT_QUIZ: openQuizWindow(); break;
                case IDC_PATCH_REFRESH: refreshPatch(); break;
                case IDC_SET_SAVE: {
                    Config cfg = Config::load();
                    cfg.overlayEnabled = IsDlgButtonChecked(hwnd, IDC_SET_OVERLAY) == BST_CHECKED;
                    cfg.lcuReadEnabled = IsDlgButtonChecked(hwnd, IDC_SET_LCU) == BST_CHECKED;
                    cfg.captureEnabled = IsDlgButtonChecked(hwnd, IDC_SET_CAPTURE) == BST_CHECKED;
                    cfg.leagueLockfilePath = n(ctl(IDC_SET_LOCKFILE));
                    cfg.save();
                    refreshSettings();
                    break;
                }
                case IDC_SET_AUDIT: showAudit(); break;
                case IDC_SET_WIPE:
                    if (MessageBoxW(hwnd,
                                    L"Esto elimina perfil, partidas, análisis, misiones y "
                                    L"progreso locales. ¿Continuar?",
                                    L"RiftLoop", MB_YESNO | MB_ICONWARNING) == IDYES) {
                        g->db->wipeAll();
                        refreshProfileUi();
                        refreshMatchList();
                        refreshSettings();
                    }
                    break;
            }
            return 0;
        case WM_APP_IPC:
            if (wp == 1) {           // analysis worker finished
                EnableWindow(ctl(IDC_ANALYZE), TRUE);
                setText(IDC_PROFILE_STATUS, std::to_string((int)lp) + " partidas analizadas.");
                refreshMatchList();
                refreshMission();
                auto rows = g->db->listMatches(1);
                if ((int)lp > 0 && !rows.empty()) showAnalysis(rows[0].matchId);
            } else if (wp == 2) {    // fetch worker finished
                EnableWindow(ctl(IDC_FETCH_API), TRUE);
                int imported = (int)lp;
                setText(IDC_PROFILE_STATUS, imported < 0
                    ? "No se pudo leer el historial: abre el cliente de League y reintenta."
                    : std::to_string(imported) +
                      " partidas nuevas del cliente. Pulsa 'Analizar pendientes'.");
                refreshMatchList();
                refreshProfileUi();  // Riot ID llega solo desde el cliente
            } else {
                processIpcQueue();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    HWND prev = FindWindowW(L"RiftLoopDesktopWnd", nullptr);
    if (prev) {
        ShowWindow(prev, SW_RESTORE);
        SetForegroundWindow(prev);
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    App app;
    g = &app;
    try {
        app.db = std::make_unique<Db>();
    } catch (const std::exception&) {
        MessageBoxW(nullptr, L"No se pudo abrir la base de datos local.", L"RiftLoop",
                    MB_ICONERROR);
        return 1;
    }
    {
        // Data language follows the League client (remembered by the Agent).
        Config cfg = Config::load();
        std::string locale = resolveDataLocale(*app.db, nullptr, cfg.dataLocale);
        app.ddOk = app.dd.load(true, locale);
    }

    app.font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
    app.mono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Consolas");

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RiftLoopDesktopWnd";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    app.hwnd = CreateWindowW(wc.lpszClassName, L"RiftLoop - sin conexión con el agente",
                             WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                             920, 600, nullptr, nullptr, hInst, nullptr);

    app.tabs = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 904, 561, app.hwnd,
                             (HMENU)IDC_TABS, hInst, nullptr);
    SendMessageW(app.tabs, WM_SETFONT, (WPARAM)app.font, TRUE);
    for (int i = 0; i < kPages; ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(kPageNames[i]);
        TabCtrl_InsertItem(app.tabs, i, &item);
    }

    buildPages();
    switchPage(0);
    ShowWindow(app.hwnd, SW_SHOW);

    app.client = std::make_unique<ipc::Client>(onIpc);
    app.client->start();

    // First-run hint (RF-ONB-001 transparency, condensed).
    if (app.db->loadProfile().pool.empty()) {
        MessageBoxW(app.hwnd,
                    L"Bienvenido a RiftLoop (iteración local).\n\n"
                    L"Qué hace este build: lee el estado del cliente de League (solo lectura), "
                    L"analiza tus partidas y guarda todo en tu equipo (SQLite local).\n\n"
                    L"Qué NO hace: no escribe runas ni hechizos, no graba video, no envía tus "
                    L"datos a ningún servidor propio.\n\n"
                    L"Empieza en Perfil: añade tu champion pool e importa partidas.",
                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(app.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
