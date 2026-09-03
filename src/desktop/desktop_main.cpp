// RiftLoop.Desktop: native Win32 panel, dark UI with Data Dragon iconography.
// Sidebar navigation + ReportView cards; the quiz opens centered over the
// League client. Listens to the Agent over IPC.
#include "core/analysis.h"
#include "core/config.h"
#include "core/contracts.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/imagecache.h"
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
#include "ui.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>

#include <algorithm>
#include <map>
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
using namespace rlui;
using nlohmann::json;

namespace {

// --------------------------------------------------------------- control ids
enum : int {
    IDC_NAV0 = 900,                  // ..906 sidebar pages
    // profile
    IDC_RIOTID = 1100, IDC_MODE_ESCALAR, IDC_MODE_APRENDER, IDC_ROLE_MAIN, IDC_ROLE_SECOND,
    IDC_POOL_LIST, IDC_POOL_CHAMP, IDC_POOL_ROLE, IDC_POOL_TIER, IDC_POOL_GAMES,
    IDC_POOL_ADD, IDC_POOL_DEL, IDC_POOL_SUGGEST, IDC_SAVE_PROFILE,
    IDC_IMPORT_JSON, IDC_FETCH_API, IDC_ANALYZE, IDC_PROFILE_STATUS,
    // matches
    IDC_MATCH_LIST = 1200, IDC_MATCH_OPEN, IDC_MATCH_REFRESH,
    // postmatch
    IDC_POST_VIEW = 1300, IDC_FB_OK, IDC_FB_PARTIAL, IDC_FB_WRONG, IDC_FB_CTX,
    IDC_ACCEPT_MISSION,
    // mission
    IDC_MISSION_VIEW = 1400, IDC_SUGGEST_MISSION, IDC_SKILLS_LIST, IDC_MISSION_REFRESH,
    // draft
    IDC_DRAFT_ROLE = 1500, IDC_DRAFT_CHAMP, IDC_DRAFT_GO, IDC_DRAFT_PLAN, IDC_DRAFT_QUIZ,
    IDC_DRAFT_VIEW,
    IDC_DRAFT_ALLY0 = 1520,          // ..1523
    IDC_DRAFT_ENEMY0 = 1530,         // ..1534
    // patch
    IDC_PATCH_VIEW = 1600, IDC_PATCH_REFRESH,
    // settings
    IDC_SET_OVERLAY = 1700, IDC_SET_LCU, IDC_SET_CAPTURE, IDC_SET_LOCKFILE, IDC_SET_SAVE,
    IDC_SET_WIPE, IDC_SET_AUDIT, IDC_SET_VIEW,
    // quiz window
    IDC_QUIZ_OPT0 = 1800,            // ..1803
    IDC_QUIZ_NEXT = 1810,
};

constexpr UINT WM_APP_IPC = WM_APP + 10;
constexpr int kPages = 7;
const wchar_t* kPageNames[kPages] = {L"Perfil", L"Partidas", L"Post-match", L"Misión",
                                     L"Draft Lab", L"Parche", L"Ajustes"};
const wchar_t* kPageSubtitles[kPages] = {
    L"Tu cuenta, tu champion pool y tus datos",
    L"Historial importado desde el cliente",
    L"Evidencia, confianza y feedback",
    L"Una misión activa, medida por oportunidades",
    L"Top 3 y plan pregame (se rellena solo en champion select)",
    L"Impacto del parche en tu pool",
    L"Módulos, privacidad y auditoría"};

constexpr int kSidebarW = 200;
constexpr int kWinW = 1180, kWinH = 780;
const char* kRoles[5] = {"TOP", "JUNGLE", "MIDDLE", "BOTTOM", "UTILITY"};

struct App {
    HWND hwnd = nullptr;
    std::vector<HWND> pageControls[kPages];
    int currentPage = 0;

    std::unique_ptr<Db> db;
    Ddragon dd;
    bool ddOk = false;
    bool modeAprender = false;

    // sorted (display name, ddragon id)
    std::vector<std::pair<std::wstring, std::string>> champList;

    HIMAGELIST poolIcons = nullptr, matchIcons = nullptr;
    std::vector<std::string> matchRowIds;

    std::unique_ptr<ipc::Client> client;
    std::string agentState = "sin agente";

    std::string currentMatchId;

    // quiz
    HWND quizWnd = nullptr;
    std::vector<QuizQuestion> quiz;
    int quizIndex = 0, quizCorrect = 0;
    bool quizAnswered = false;
    int quizChosen = -1;

    std::mutex ipcMutex;
    std::vector<std::string> ipcQueue;
};

App* g = nullptr;

// ------------------------------------------------------------------ helpers

std::wstring w(const std::string& s) { return util::widen(s); }
std::string n(HWND ctl) {
    wchar_t buf[2048];
    GetWindowTextW(ctl, buf, 2048);
    return util::narrow(buf);
}
HWND ctl(int id) { return GetDlgItem(g->hwnd, id); }
void setText(int id, const std::string& utf8) { SetWindowTextW(ctl(id), w(utf8).c_str()); }

std::string champDisplay(const std::string& id) {
    if (g->ddOk)
        if (const ChampInfo* c = g->dd.champion(id)) return c->name;
    return id;
}

std::string champIconUrl(const std::string& id) {
    return g->ddOk ? g->dd.championIconUrl(id) : "";
}

// Combo helpers: champion combos carry "—" at index 0; item data = champList idx.
void fillChampCombo(HWND cb) {
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"—");
    for (auto& [name, id] : g->champList)
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)name.c_str());
    SendMessageW(cb, CB_SETCURSEL, 0, 0);
}
std::string comboChampId(int ctlId) {
    int sel = (int)SendMessageW(ctl(ctlId), CB_GETCURSEL, 0, 0);
    if (sel <= 0 || sel > (int)g->champList.size()) return {};
    return g->champList[sel - 1].second;
}
void fillRoleCombo(HWND cb, bool withNone) {
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    if (withNone) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"—");
    for (auto* r : {L"TOP", L"JUNGLE", L"MIDDLE", L"BOTTOM", L"UTILITY"})
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)r);
    SendMessageW(cb, CB_SETCURSEL, withNone ? 0 : 2, 0);
}
std::string comboRole(int ctlId, bool withNone) {
    int sel = (int)SendMessageW(ctl(ctlId), CB_GETCURSEL, 0, 0);
    if (withNone) {
        if (sel <= 0) return {};
        --sel;
    }
    return (sel >= 0 && sel < 5) ? kRoles[sel] : "";
}

HWND mk(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int cx,
        int cy, int id) {
    HWND h = CreateWindowW(cls, text, WS_CHILD | style, x, y, cx, cy, g->hwnd,
                           (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)theme::body(), TRUE);
    applyDarkTheme(h);
    g->pageControls[page].push_back(h);
    return h;
}

HWND mkButton(int page, const wchar_t* text, int x, int y, int cx, int cy, int id) {
    return mk(page, L"BUTTON", text, WS_VISIBLE | BS_OWNERDRAW, x, y, cx, cy, id);
}

HWND mkReport(int page, int x, int y, int cx, int cy, int id) {
    HWND h = CreateWindowW(L"RiftLoopReport", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, x, y, cx,
                           cy, g->hwnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
    g->pageControls[page].push_back(h);
    return h;
}

// 32bpp scale for image lists.
HBITMAP scaledCopy(HBITMAP src, int size) {
    if (!src) return nullptr;
    BITMAP info{};
    GetObjectW(src, sizeof info, &info);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP out = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!out) return nullptr;
    HDC dst = CreateCompatibleDC(nullptr);
    HDC srcDc = CreateCompatibleDC(nullptr);
    HGDIOBJ o1 = SelectObject(dst, out);
    HGDIOBJ o2 = SelectObject(srcDc, src);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dst, 0, 0, size, size, srcDc, 0, 0, info.bmWidth, info.bmHeight, bf);
    SelectObject(dst, o1);
    SelectObject(srcDc, o2);
    DeleteDC(dst);
    DeleteDC(srcDc);
    return out;
}

int champImageIndex(HIMAGELIST iml, std::map<std::string, int>& cache, const std::string& id) {
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    int idx = -1;
    if (HBITMAP bmp = icons::get("champ", id, champIconUrl(id))) {
        if (HBITMAP small2 = scaledCopy(bmp, 28)) {
            idx = ImageList_Add(iml, small2, nullptr);
            DeleteObject(small2);
        }
    }
    cache[id] = idx;
    return idx;
}

std::wstring confBadge(const std::string& conf) { return L"confianza " + w(conf); }
COLORREF confColor(const std::string& conf) {
    if (conf == "alta") return theme::kGood;
    if (conf == "media") return theme::kWarn;
    return theme::kDim;
}

// ------------------------------------------------------- report view builders

std::vector<RVItem> rvTop3(const Top3& t) {
    std::vector<RVItem> v;
    v.push_back({RVKind::Title, L"Top 3 para esta partida"});
    if (!t.available) {
        v.push_back({RVKind::Text, w(t.unavailableReason)});
        return v;
    }
    v.push_back({RVKind::Dim, L"Incertidumbre: " + w(t.uncertaintyReason)});
    for (auto& c : t.cards) {
        RVItem title{RVKind::Section, w(c.label)};
        v.push_back(title);
        RVItem champ{RVKind::IconRow, w(champDisplay(c.champion))};
        champ.iconKind = "champ";
        champ.iconId = c.champion;
        champ.iconUrl = champIconUrl(c.champion);
        champ.right = confBadge(c.confidence);
        v.push_back(champ);
        for (auto& r : c.reasons) v.push_back({RVKind::Text, L"+ " + w(r), L"", "", "", "", 0, 12});
        RVItem risk{RVKind::Dim, L"Riesgo: " + w(c.risk)};
        risk.indent = 12;
        v.push_back(risk);
        RVItem xp{RVKind::Dim, w(c.experience)};
        xp.indent = 12;
        v.push_back(xp);
    }
    return v;
}

void rvAddItemRow(std::vector<RVItem>& v, int itemId, const std::wstring& note, int indent) {
    std::wstring name = L"Item " + std::to_wstring(itemId);
    if (g->ddOk)
        if (const ItemInfo* it = g->dd.item(itemId)) name = w(it->name);
    RVItem row{RVKind::IconRow, name, note};
    row.iconKind = "item";
    row.iconId = std::to_string(itemId);
    row.iconUrl = g->ddOk ? g->dd.itemIconUrl(itemId) : "";
    row.indent = indent;
    v.push_back(row);
}

void rvAddPerkRow(std::vector<RVItem>& v, int perkId, const std::wstring& label, int indent) {
    RVItem row{RVKind::IconRow, w(g->dd.perkName(perkId)), label};
    row.iconKind = "perk";
    row.iconId = std::to_string(perkId);
    row.iconUrl = g->dd.perkIconUrl(perkId);
    row.indent = indent;
    v.push_back(row);
}

std::vector<RVItem> rvPlan(const std::string& champ, const std::string& role, const RunePlan& rp,
                           const SpellPlan& sp, const ItemPlan& ip,
                           const std::vector<QuizQuestion>& quiz) {
    std::vector<RVItem> v;
    RVItem title{RVKind::Title, w(champDisplay(champ)) + L" · " + w(role)};
    title.iconKind = "champ";
    title.iconId = champ;
    title.iconUrl = champIconUrl(champ);
    v.push_back(title);

    if (g->ddOk && rp.main.perks.size() >= 9) {
        v.push_back({RVKind::Section, L"Runas — " + w(g->dd.styleName(rp.main.primaryStyle)) +
                                          L" + " + w(g->dd.styleName(rp.main.subStyle))});
        RVItem badge{RVKind::Badge, confBadge(rp.confidence)};
        badge.color = confColor(rp.confidence);
        v.push_back(badge);
        rvAddPerkRow(v, rp.main.perks[0], L"piedra angular", 0);
        for (int i = 1; i <= 3; ++i) rvAddPerkRow(v, rp.main.perks[i], L"", 12);
        rvAddPerkRow(v, rp.main.perks[4], L"secundaria", 12);
        rvAddPerkRow(v, rp.main.perks[5], L"secundaria", 12);
        v.push_back({RVKind::Dim, L"Fragmentos: " + w(g->dd.shardName(rp.main.perks[6])) + L" · " +
                                      w(g->dd.shardName(rp.main.perks[7])) + L" · " +
                                      w(g->dd.shardName(rp.main.perks[8]))});
        for (auto& r : rp.main.reasons) v.push_back({RVKind::Dim, L"• " + w(r)});
        if (rp.situational && rp.situational->perks.size() >= 6) {
            v.push_back({RVKind::Dim,
                         L"Alternativa: " + w(g->dd.styleName(rp.situational->subStyle)) +
                             L" secundaria con " + w(g->dd.perkName(rp.situational->perks[4])) +
                             L" + " + w(g->dd.perkName(rp.situational->perks[5]))});
        }
    }

    v.push_back({RVKind::Section, L"Hechizos"});
    for (auto& spell : sp.spells) {
        RVItem row{RVKind::IconRow, w(g->ddOk ? g->dd.summonerDisplay(spell) : spell)};
        row.iconKind = "spell";
        row.iconId = spell;
        row.iconUrl = g->ddOk ? g->dd.spellIconUrl(spell) : "";
        v.push_back(row);
    }
    v.push_back({RVKind::Dim, w(sp.reason)});

    v.push_back({RVKind::Section, L"Plan de items"});
    RVItem ibadge{RVKind::Badge, confBadge(ip.confidence)};
    ibadge.color = confColor(ip.confidence);
    v.push_back(ibadge);
    if (!ip.starting.empty()) {
        v.push_back({RVKind::Text, L"Inicio:"});
        for (int id : ip.starting) rvAddItemRow(v, id, L"", 12);
    }
    if (!ip.core.empty()) {
        v.push_back({RVKind::Text, L"Núcleo (de tu propio historial):"});
        for (int id : ip.core) rvAddItemRow(v, id, L"", 12);
    }
    for (auto& b : ip.boots) {
        v.push_back({RVKind::Text, w(b.label) + L":"});
        for (int id : b.items) rvAddItemRow(v, id, L"", 12);
        RVItem cond{RVKind::Dim, w(b.condition)};
        cond.indent = 12;
        v.push_back(cond);
    }
    for (auto& b : ip.branches) {
        v.push_back({RVKind::Text, w(b.label) + L":"});
        for (int id : b.items) rvAddItemRow(v, id, L"", 12);
        RVItem cond{RVKind::Dim, w(b.condition)};
        cond.indent = 12;
        v.push_back(cond);
    }
    v.push_back({RVKind::Dim, w(ip.datasetNote)});

    if (!quiz.empty()) {
        v.push_back({RVKind::Section, L"Quiz de loading listo"});
        for (auto& q : quiz) v.push_back({RVKind::Dim, L"• " + w(q.text)});
    }
    return v;
}

std::vector<RVItem> rvAnalysis(const AnalysisResult& a, const MatchRow* row) {
    std::vector<RVItem> v;
    RVItem title{RVKind::Title, row ? w(champDisplay(row->userChampion)) + L" · " +
                                          w(row->userRole) +
                                          (row->userWin ? L" · VICTORIA" : L" · DERROTA")
                                    : w(a.matchId)};
    if (row && !row->userChampion.empty()) {
        title.iconKind = "champ";
        title.iconId = row->userChampion;
        title.iconUrl = champIconUrl(row->userChampion);
    }
    v.push_back(title);
    if (row)
        v.push_back({RVKind::Dim, std::to_wstring(row->durationSec / 60) + L" min · parche " +
                                      w(row->patch) + L" · " + w(a.matchId)});

    v.push_back({RVKind::Section, L"Fortaleza", L"", "", "", "", theme::kGood});
    v.push_back({RVKind::Text, w(a.strength)});

    int shown = 0;
    for (auto& f : a.findings) {
        if (f.failures == 0) continue;
        ++shown;
        if (shown > 3) break;
        v.push_back({RVKind::Section,
                     shown == 1 ? L"Patrón prioritario — " + w(f.title) : w(f.title),
                     L"", "", "", "", shown == 1 ? theme::kWarn : theme::kAccent});
        RVItem badge{RVKind::Badge,
                     confBadge(f.confidence) + L" · " + std::to_wstring(f.failures) + L" de " +
                         std::to_wstring(f.opportunities) + L" oportunidades falladas"};
        badge.color = confColor(f.confidence);
        v.push_back(badge);
        v.push_back({RVKind::Text, L"Por qué importa: " + w(f.whyItMatters)});
        v.push_back({RVKind::Text, L"Qué probar: " + w(f.alternative)});
        int evn = 0;
        for (auto& ev : f.evidence) {
            if (++evn > 3) break;    // cognitive budget (PRD 10.2)
            v.push_back({RVKind::Text,
                         L"[" + w(util::formatGameClock(ev.gameTimestampMs)) + L"] " +
                             w(ev.observedFacts),
                         L"", "", "", "", 0, 12});
            RVItem inf{RVKind::Dim, L"Inferencia (" + w(ev.confidence) + L"): " + w(ev.inference)};
            inf.indent = 24;
            v.push_back(inf);
            RVItem exc{RVKind::Dim, L"Exclusiones: " + w(ev.exclusionsChecked)};
            exc.indent = 24;
            v.push_back(exc);
        }
    }
    if (shown == 0)
        v.push_back({RVKind::Text, L"Sin patrones con fallos detectados en esta partida."});
    v.push_back({RVKind::Section, L"Limitaciones", L"", "", "", "", theme::kDim});
    v.push_back({RVKind::Dim, w(a.limitations)});
    return v;
}

std::vector<RVItem> rvMission() {
    std::vector<RVItem> v;
    if (auto prog = activeMissionProgress(*g->db)) {
        auto& m = prog->mission;
        v.push_back({RVKind::Title, L"Misión activa"});
        v.push_back({RVKind::Section, w(m.name)});
        v.push_back({RVKind::Text, L"Hipótesis: " + w(m.hypothesis)});
        v.push_back({RVKind::Text, L"Métrica: " + w(m.metric)});
        v.push_back({RVKind::Dim, L"Aplica: " + w(m.appliesWhen)});
        v.push_back({RVKind::Dim, L"No aplica: " + w(m.doesNotApply)});
        RVItem badge{RVKind::Badge,
                     std::to_wstring(prog->gamesTracked) + L"/" + std::to_wstring(m.blockSize) +
                         L" partidas · " + std::to_wstring(prog->successes) + L" de " +
                         std::to_wstring(prog->validOpportunities) + L" oportunidades ejecutadas"};
        badge.color = theme::kAccent;
        v.push_back(badge);
    } else {
        v.push_back({RVKind::Title, L"Sin misión activa"});
        bool suggested = false;
        for (auto& m : g->db->listMissions()) {
            if (m.status == MissionStatus::Suggested && !suggested) {
                suggested = true;
                v.push_back({RVKind::Section, L"Sugerida: " + w(m.name), L"", "", "", "",
                             theme::kWarn});
                v.push_back({RVKind::Text, L"Métrica: " + w(m.metric)});
                v.push_back({RVKind::Dim, L"Acéptala con el botón de abajo."});
            }
            if (m.status == MissionStatus::Evaluated)
                v.push_back({RVKind::Dim, L"Evaluada: " + w(m.name) + L" → " +
                                              w(toString(m.result))});
        }
        if (!suggested)
            v.push_back({RVKind::Text, L"Analiza partidas y pulsa \"Sugerir misión\"."});
    }
    v.push_back({RVKind::Section, L"Progreso saludable"});
    v.push_back({RVKind::Text, L"Racha de mejora: " + std::to_wstring(g->db->streakDays()) +
                                   L" día(s) · XP: " + std::to_wstring(g->db->totalXp())});
    v.push_back({RVKind::Dim, L"La racha se mantiene con un quiz, una evidencia revisada o una "
                              L"partida con misión. No exige jugar."});
    return v;
}

std::vector<RVItem> rvPatch() {
    std::vector<RVItem> v;
    if (!g->ddOk) {
        v.push_back({RVKind::Title, L"Sin datos estáticos"});
        return v;
    }
    auto rep = patchImpact(*g->db, g->dd);
    v.push_back({RVKind::Title, L"Parche " + w(g->dd.displayPatch())});
    v.push_back({RVKind::Dim, L"Datos Data Dragon " + w(g->dd.version()) + L" · idioma " +
                                  w(g->dd.locale()) +
                                  (rep.fromVersion.empty()
                                       ? L" · sin snapshot anterior para comparar"
                                       : L" · comparado con " + w(rep.fromVersion))});
    for (auto& e : rep.entries) {
        RVItem row{RVKind::IconRow, w(champDisplay(e.champion))};
        row.iconKind = "champ";
        row.iconId = e.champion;
        row.iconUrl = champIconUrl(e.champion);
        row.color = e.direct ? theme::kWarn : theme::kText;
        v.push_back(row);
        RVItem det{RVKind::Dim, w(e.change)};
        det.indent = 34;
        v.push_back(det);
    }
    v.push_back({RVKind::Spacer});
    v.push_back({RVKind::Dim, w(rep.note)});
    return v;
}

std::vector<RVItem> rvSettingsInfo() {
    Config cfg = Config::load();
    std::vector<RVItem> v;
    v.push_back({RVKind::Section, L"Estado"});
    v.push_back({RVKind::Text, L"Agente/cliente: " + w(g->agentState)});
    v.push_back({RVKind::Text, L"Datos: " + (g->ddOk ? L"parche " + w(g->dd.displayPatch()) +
                                                           L" (Data Dragon " + w(g->dd.version()) +
                                                           L", " + w(g->dd.locale()) + L")"
                                                     : std::wstring(L"no disponibles"))});
    v.push_back({RVKind::Text, L"Carpeta local: " + util::widen(util::dataDir().string())});
    v.push_back({RVKind::Section, L"Privacidad"});
    v.push_back({RVKind::Dim, L"Todo se guarda en tu equipo (SQLite local). Este build no "
                              L"escribe nada en el cliente de League y no envía datos a "
                              L"servidores propios."});
    v.push_back({RVKind::Dim, L"Grabación: " + std::wstring(cfg.captureEnabled
                                                                ? L"activada (beta, MP4 local)"
                                                                : L"desactivada (opt-in)")});
    return v;
}

// ------------------------------------------------------------- page refresh

void refreshMatchList() {
    HWND lv = ctl(IDC_MATCH_LIST);
    ListView_DeleteAllItems(lv);
    ImageList_RemoveAll(g->matchIcons);
    std::map<std::string, int> iconIdx;
    g->matchRowIds.clear();
    int i = 0;
    for (auto& r : g->db->listMatches(50)) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = i;
        std::wstring champ = w(champDisplay(r.userChampion));
        item.pszText = champ.data();
        item.iImage = r.userChampion.empty() ? -1
                                             : champImageIndex(g->matchIcons, iconIdx,
                                                               r.userChampion);
        item.lParam = (LPARAM)g->matchRowIds.size();
        g->matchRowIds.push_back(r.matchId);
        ListView_InsertItem(lv, &item);
        auto set = [&](int col, const std::string& s) {
            std::wstring ws = w(s);
            ListView_SetItemText(lv, i, col, ws.data());
        };
        set(1, r.userRole);
        set(2, r.userWin ? "Victoria" : "Derrota");
        set(3, std::to_string(r.durationSec / 60) + " min");
        set(4, r.patch);
        set(5, r.analyzed ? "analizada" : "pendiente");
        ++i;
    }
}

void refreshProfileUi() {
    Profile p = g->db->loadProfile();
    setText(IDC_RIOTID, p.riotId.empty() ? "(se detecta del cliente de League)" : p.riotId);
    g->modeAprender = p.mode == AppMode::Aprender;
    InvalidateRect(ctl(IDC_MODE_ESCALAR), nullptr, TRUE);
    InvalidateRect(ctl(IDC_MODE_APRENDER), nullptr, TRUE);

    auto selRole = [&](int id, const std::string& role, bool withNone) {
        HWND cb = ctl(id);
        int base = withNone ? 1 : 0;
        int target = withNone ? 0 : 2;
        for (int i = 0; i < 5; ++i)
            if (role == kRoles[i]) target = i + base;
        SendMessageW(cb, CB_SETCURSEL, target, 0);
    };
    selRole(IDC_ROLE_MAIN, p.preferredRoles.empty() ? "MIDDLE" : p.preferredRoles[0], false);
    selRole(IDC_ROLE_SECOND, p.preferredRoles.size() > 1 ? p.preferredRoles[1] : "", true);

    HWND lv = ctl(IDC_POOL_LIST);
    ListView_DeleteAllItems(lv);
    ImageList_RemoveAll(g->poolIcons);
    std::map<std::string, int> iconIdx;
    int i = 0;
    for (auto& e : p.pool) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = i;
        std::wstring champ = w(champDisplay(e.champion));
        item.pszText = champ.data();
        item.iImage = champImageIndex(g->poolIcons, iconIdx, e.champion);
        ListView_InsertItem(lv, &item);
        auto set = [&](int col, const std::string& s) {
            std::wstring ws = w(s);
            ListView_SetItemText(lv, i, col, ws.data());
        };
        set(1, e.role);
        set(2, e.tier == PoolTier::Main ? "main"
             : e.tier == PoolTier::Comfort ? "cómodo"
             : e.tier == PoolTier::Learning ? "aprendiendo" : "no recomendar");
        set(3, std::to_string(e.declaredGames));
        ++i;
    }
}

void refreshMission() {
    rvSet(ctl(IDC_MISSION_VIEW), rvMission());
    HWND lv = ctl(IDC_SKILLS_LIST);
    ListView_DeleteAllItems(lv);
    int i = 0;
    for (auto& s : g->db->loadSkills()) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        std::wstring dom = w(s.domain);
        item.pszText = dom.data();
        ListView_InsertItem(lv, &item);
        std::wstring st = w(toString(s.state));
        ListView_SetItemText(lv, i, 1, st.data());
        std::wstring conf = w(s.confidence + " (" + std::to_string(s.opportunitiesSeen) + ")");
        ListView_SetItemText(lv, i, 2, conf.data());
        ++i;
    }
}

void switchPage(int page);

void showAnalysis(const std::string& matchId) {
    auto a = g->db->loadAnalysis(matchId);
    if (!a) return;
    g->currentMatchId = matchId;
    MatchRow rowCopy;
    const MatchRow* rowPtr = nullptr;
    for (auto& r : g->db->listMatches(100))
        if (r.matchId == matchId) { rowCopy = r; rowPtr = &rowCopy; break; }
    rvSet(ctl(IDC_POST_VIEW), rvAnalysis(*a, rowPtr));
    switchPage(2);
}

void refreshSettings() {
    Config cfg = Config::load();
    CheckDlgButton(g->hwnd, IDC_SET_OVERLAY, cfg.overlayEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_LCU, cfg.lcuReadEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_CAPTURE, cfg.captureEnabled ? BST_CHECKED : 0);
    setText(IDC_SET_LOCKFILE, cfg.leagueLockfilePath);
    rvSet(ctl(IDC_SET_VIEW), rvSettingsInfo());
}

// ------------------------------------------------------------------- actions

void saveProfileFromUi() {
    Profile p = g->db->loadProfile();
    std::string typed = n(ctl(IDC_RIOTID));
    if (typed.find("(se detecta") == std::string::npos) p.riotId = typed;
    p.mode = g->modeAprender ? AppMode::Aprender : AppMode::Escalar;
    p.preferredRoles.clear();
    p.preferredRoles.push_back(comboRole(IDC_ROLE_MAIN, false));
    std::string second = comboRole(IDC_ROLE_SECOND, true);
    if (!second.empty()) p.preferredRoles.push_back(second);
    g->db->saveProfile(p);
    setText(IDC_PROFILE_STATUS, "Perfil guardado.");
}

void addPoolEntry() {
    std::string champ = comboChampId(IDC_POOL_CHAMP);
    if (champ.empty()) {
        setText(IDC_PROFILE_STATUS, "Elige un campeón en el desplegable.");
        return;
    }
    Profile p = g->db->loadProfile();
    PoolEntry e;
    e.champion = champ;
    e.role = comboRole(IDC_POOL_ROLE, false);
    int tierSel = (int)SendMessageW(ctl(IDC_POOL_TIER), CB_GETCURSEL, 0, 0);
    e.tier = tierSel == 0 ? PoolTier::Main
           : tierSel == 2 ? PoolTier::Learning
           : tierSel == 3 ? PoolTier::DoNotRecommend : PoolTier::Comfort;
    e.declaredGames = atoi(n(ctl(IDC_POOL_GAMES)).c_str());
    p.pool.push_back(e);
    g->db->saveProfile(p);
    refreshProfileUi();
    setText(IDC_PROFILE_STATUS, champDisplay(champ) + " añadido al pool.");
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

// Builds the pool from the player's own history: no manual typing (user ask).
void suggestPoolFromHistory() {
    struct Agg { int games = 0; std::string role; };
    std::map<std::string, Agg> agg;
    for (auto& r : g->db->listMatches(80)) {
        if (r.userChampion.empty()) continue;
        Agg& a = agg[r.userChampion];
        ++a.games;
        if (a.role.empty()) a.role = r.userRole;
    }
    if (agg.empty()) {
        setText(IDC_PROFILE_STATUS,
                "Sin historial: descarga partidas del cliente y analiza primero.");
        return;
    }
    std::vector<std::pair<int, std::string>> ranked;
    for (auto& [champ, a] : agg) ranked.push_back({a.games, champ});
    std::sort(ranked.rbegin(), ranked.rend());

    Profile p = g->db->loadProfile();
    int added = 0;
    for (size_t i = 0; i < ranked.size() && i < 8; ++i) {
        auto& [games, champ] = ranked[i];
        if (games < 2) break;
        bool exists = false;
        for (auto& e : p.pool)
            if (e.champion == champ) exists = true;
        if (exists) continue;
        PoolEntry e;
        e.champion = champ;
        e.role = agg[champ].role;
        e.tier = i < 2 ? PoolTier::Main : PoolTier::Comfort;
        e.declaredGames = games;
        p.pool.push_back(e);
        ++added;
    }
    g->db->saveProfile(p);
    refreshProfileUi();
    setText(IDC_PROFILE_STATUS, std::to_string(added) +
                                " campeones añadidos desde tu historial. Ajusta niveles si quieres.");
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
            Db db;
            done = analyzePending(db, g->ddOk ? &g->dd : nullptr);
        } catch (...) {}
        PostMessageW(g->hwnd, WM_APP_IPC, 1, (LPARAM)done);
    }).detach();
}

void runFetch() {
    setText(IDC_PROFILE_STATUS, "Descargando historial desde el cliente de League...");
    EnableWindow(ctl(IDC_FETCH_API), FALSE);
    std::thread([] {
        int imported = -1;
        try {
            Db db;
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

DraftContext draftContextFromUi() {
    DraftContext ctx;
    ctx.role = comboRole(IDC_DRAFT_ROLE, false);
    for (int i = 0; i < 4; ++i) {
        std::string id = comboChampId(IDC_DRAFT_ALLY0 + i);
        if (!id.empty()) ctx.allyChampions.push_back(id);
    }
    for (int i = 0; i < 5; ++i) {
        std::string id = comboChampId(IDC_DRAFT_ENEMY0 + i);
        if (!id.empty()) ctx.enemyChampions.push_back(id);
    }
    ctx.patch = g->ddOk ? g->dd.version() : "";
    return ctx;
}

void draftRecommend() {
    if (!g->ddOk) return;
    DraftContext ctx = draftContextFromUi();
    Top3 top = recommendTop3(*g->db, g->dd, ctx);
    json contract = makeContract("top3", ctx.patch, json{{"role", ctx.role}}, json(top),
                                 top.cards.empty() ? "baja" : top.cards[0].confidence,
                                 top.uncertaintyReason);
    g->db->saveRecommendation("top3", contract.dump());
    rvSet(ctl(IDC_DRAFT_VIEW), rvTop3(top));
}

void draftPlan() {
    if (!g->ddOk) return;
    std::string champ = comboChampId(IDC_DRAFT_CHAMP);
    if (champ.empty()) {
        rvSet(ctl(IDC_DRAFT_VIEW),
              {{RVKind::Title, L"Elige tu campeón"},
               {RVKind::Text, L"Selecciona un campeón en el desplegable \"Tu campeón\"."}});
        return;
    }
    PlanInput pi;
    pi.champion = champ;
    pi.draft = draftContextFromUi();
    pi.role = pi.draft.role;
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
    rvSet(ctl(IDC_DRAFT_VIEW), rvPlan(pi.champion, pi.role, rp, sp, ip, g->quiz));
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

// ------------------------------------------------------------------ quiz ui

HWND findLeagueWindow() {
    // Loading screen / in game window first, then the client.
    if (HWND h = FindWindowW(nullptr, L"League of Legends (TM) Client")) return h;
    if (HWND h = FindWindowW(L"RCLIENT", nullptr)) return h;
    if (HWND h = FindWindowW(nullptr, L"League of Legends")) return h;
    return nullptr;
}

void quizLayout();

LRESULT CALLBACK QuizProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, theme::bgBrush());
            SetBkMode(dc, TRANSPARENT);
            if (g->quizIndex < (int)g->quiz.size()) {
                auto& q = g->quiz[g->quizIndex];
                SelectObject(dc, theme::tiny());
                SetTextColor(dc, theme::kAccent);
                std::wstring prog = L"PREGUNTA " + std::to_wstring(g->quizIndex + 1) + L" DE " +
                                    std::to_wstring(g->quiz.size());
                TextOutW(dc, 24, 18, prog.c_str(), (int)prog.size());
                SelectObject(dc, theme::h2());
                SetTextColor(dc, theme::kText);
                RECT qr{24, 42, rc.right - 24, 118};
                DrawTextW(dc, w(q.text).c_str(), -1, &qr, DT_WORDBREAK);
                if (g->quizAnswered) {
                    SelectObject(dc, theme::small_());
                    bool right = g->quizChosen == q.correctIndex;
                    SetTextColor(dc, right ? theme::kGood : theme::kDanger);
                    std::wstring head = right ? L"Correcto." : L"No exactamente.";
                    TextOutW(dc, 24, rc.bottom - 84, head.c_str(), (int)head.size());
                    SetTextColor(dc, theme::kDim);
                    RECT er{24, rc.bottom - 62, rc.right - 140, rc.bottom - 12};
                    DrawTextW(dc, w(q.explanation).c_str(), -1, &er, DT_WORDBREAK);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            auto* dis = (DRAWITEMSTRUCT*)lp;
            int id = (int)dis->CtlID;
            HDC dc = dis->hDC;
            RECT rc = dis->rcItem;
            bool isNext = id == IDC_QUIZ_NEXT;
            int optIdx = id - IDC_QUIZ_OPT0;
            auto& q = g->quiz[g->quizIndex];

            COLORREF border = theme::kBorder;
            COLORREF textCol = theme::kText;
            if (!isNext && g->quizAnswered) {
                if (optIdx == q.correctIndex) { border = theme::kGood; textCol = theme::kGood; }
                else if (optIdx == g->quizChosen) { border = theme::kDanger; textCol = theme::kDanger; }
                else textCol = theme::kDim;
            }
            HBRUSH fill = CreateSolidBrush(isNext ? theme::kCardHi : theme::kCard);
            FillRect(dc, &rc, fill);
            DeleteObject(fill);
            HBRUSH frame = CreateSolidBrush(isNext ? theme::kAccent : border);
            FrameRect(dc, &rc, frame);
            DeleteObject(frame);

            SetBkMode(dc, TRANSPARENT);
            wchar_t textBuf[256];
            GetWindowTextW(dis->hwndItem, textBuf, 256);
            int x = rc.left + 14;
            if (!isNext) {
                // Champion icon when the option is a champion name.
                std::string opt = util::narrow(textBuf);
                for (auto& [display, cid] : g->champList) {
                    if (util::narrow(display) == opt) {
                        if (HBITMAP bmp = icons::get("champ", cid, champIconUrl(cid))) {
                            drawBitmap(dc, bmp, x, rc.top + 6, 28);
                        }
                        x += 36;
                        break;
                    }
                }
            }
            SelectObject(dc, theme::body());
            SetTextColor(dc, isNext ? theme::kAccent : textCol);
            RECT tr{x, rc.top, rc.right - 8, rc.bottom};
            DrawTextW(dc, textBuf, -1, &tr, DT_SINGLELINE | DT_VCENTER |
                                                (isNext ? DT_CENTER : DT_LEFT));
            return TRUE;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id >= IDC_QUIZ_OPT0 && id <= IDC_QUIZ_OPT0 + 3 && !g->quizAnswered) {
                g->quizAnswered = true;
                g->quizChosen = id - IDC_QUIZ_OPT0;
                if (g->quizChosen == g->quiz[g->quizIndex].correctIndex) ++g->quizCorrect;
                ShowWindow(GetDlgItem(hwnd, IDC_QUIZ_NEXT), SW_SHOW);
                InvalidateRect(hwnd, nullptr, TRUE);
                for (int i = 0; i < 4; ++i)
                    InvalidateRect(GetDlgItem(hwnd, IDC_QUIZ_OPT0 + i), nullptr, TRUE);
            } else if (id == IDC_QUIZ_NEXT) {
                ++g->quizIndex;
                if (g->quizIndex >= (int)g->quiz.size()) {
                    try {
                        g->db->recordActivity(util::todayLocal(), "quiz_completed");
                        g->db->addXp(10 + 5 * g->quizCorrect, "quiz de loading");
                    } catch (...) {}
                    DestroyWindow(hwnd);
                } else {
                    g->quizAnswered = false;
                    g->quizChosen = -1;
                    quizLayout();
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }
        case WM_CTLCOLORBTN:
            return (LRESULT)theme::bgBrush();
        case WM_CLOSE:                   // closing never punishes the streak (RF-QUIZ-003)
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g->quizWnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void quizLayout() {
    if (!g->quizWnd || g->quizIndex >= (int)g->quiz.size()) return;
    auto& q = g->quiz[g->quizIndex];
    RECT rc;
    GetClientRect(g->quizWnd, &rc);
    int y = 126;
    for (int i = 0; i < 4; ++i) {
        HWND b = GetDlgItem(g->quizWnd, IDC_QUIZ_OPT0 + i);
        if (i < (int)q.options.size()) {
            SetWindowTextW(b, w(q.options[i]).c_str());
            SetWindowPos(b, nullptr, 24, y, rc.right - 48, 40, SWP_NOZORDER | SWP_SHOWWINDOW);
            y += 48;
        } else {
            ShowWindow(b, SW_HIDE);
        }
        InvalidateRect(b, nullptr, TRUE);
    }
    HWND next = GetDlgItem(g->quizWnd, IDC_QUIZ_NEXT);
    SetWindowTextW(next, g->quizIndex + 1 >= (int)g->quiz.size() ? L"Terminar" : L"Siguiente →");
    SetWindowPos(next, nullptr, rc.right - 134, rc.bottom - 52, 110, 36, SWP_NOZORDER);
    ShowWindow(next, SW_HIDE);
}

void openQuizWindow() {
    if (g->quizWnd) return;
    if (g->quiz.empty()) {
        try {
            std::string stored = g->db->lastRecommendation("quiz");
            if (!stored.empty()) g->quiz = json::parse(stored).get<std::vector<QuizQuestion>>();
        } catch (...) {}
    }
    if (g->quiz.empty()) return;
    g->quizIndex = 0;
    g->quizCorrect = 0;
    g->quizAnswered = false;
    g->quizChosen = -1;

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = QuizProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"RiftLoopQuizWnd";
        wc.hbrBackground = theme::bgBrush();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    // Center over the League window (user ask); fall back to the screen.
    int qw = 520, qh = 430;
    int x, y;
    RECT lr;
    HWND league = findLeagueWindow();
    if (league && GetWindowRect(league, &lr) && lr.right - lr.left > 300) {
        x = lr.left + ((lr.right - lr.left) - qw) / 2;
        y = lr.top + ((lr.bottom - lr.top) - qh) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - qw) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - qh) / 2;
    }

    g->quizWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"RiftLoopQuizWnd",
                                 L"RiftLoop · Quiz de loading",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, qw, qh, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    applyDarkTitleBar(g->quizWnd);
    for (int i = 0; i < 4; ++i)
        CreateWindowW(L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g->quizWnd,
                      (HMENU)(INT_PTR)(IDC_QUIZ_OPT0 + i), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Siguiente →", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g->quizWnd,
                  (HMENU)IDC_QUIZ_NEXT, nullptr, nullptr);
    quizLayout();
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
                std::string s = j.value("state", "?");
                if (s != g->agentState) {
                    g->agentState = s;
                    RECT rc{0, kWinH - 120, kSidebarW, kWinH};
                    InvalidateRect(g->hwnd, &rc, FALSE);
                    // Hook safety: open/close the quiz by state too.
                    if (s == "Loading") openQuizWindow();
                    if (s == "InGame" && g->quizWnd) DestroyWindow(g->quizWnd);
                }
            } else if (type == "top3") {
                Top3 top = j.at("data").at("options").get<Top3>();
                rvSet(ctl(IDC_DRAFT_VIEW), rvTop3(top));
                if (g->currentPage != 4) switchPage(4);      // live champ select focus
            } else if (type == "plan") {
                const json& d = j.at("data").at("options");
                RunePlan rp = d.at("runes").get<RunePlan>();
                SpellPlan sp = d.at("spells").get<SpellPlan>();
                ItemPlan ip = d.at("items").get<ItemPlan>();
                g->quiz = j.value("quiz", std::vector<QuizQuestion>{});
                rvSet(ctl(IDC_DRAFT_VIEW),
                      rvPlan(d.value("champion", ""), d.value("role", ""), rp, sp, ip, g->quiz));
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

// --------------------------------------------------------------- page layout

void addListViewColumns(HWND lv, const std::vector<std::pair<const wchar_t*, int>>& cols) {
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(lv, theme::kCard);
    ListView_SetTextBkColor(lv, theme::kCard);
    ListView_SetTextColor(lv, theme::kText);
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
    const int X = kSidebarW + 28;    // content origin
    const int Y = 108;
    const int W = kWinW - kSidebarW - 72;

    // ---- 0: Perfil ---------------------------------------------------------
    mk(0, L"STATIC", L"Riot ID", WS_VISIBLE, X, Y, 90, 20, 0);
    mk(0, L"EDIT", L"", WS_VISIBLE | WS_BORDER, X, Y + 22, 240, 26, IDC_RIOTID);
    mk(0, L"STATIC", L"Modo", WS_VISIBLE, X + 260, Y, 90, 20, 0);
    mkButton(0, L"Escalar", X + 260, Y + 22, 100, 26, IDC_MODE_ESCALAR);
    mkButton(0, L"Aprender", X + 362, Y + 22, 100, 26, IDC_MODE_APRENDER);
    mk(0, L"STATIC", L"Rol principal", WS_VISIBLE, X + 490, Y, 100, 20, 0);
    fillRoleCombo(mk(0, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, X + 490,
                     Y + 22, 120, 300, IDC_ROLE_MAIN), false);
    mk(0, L"STATIC", L"Secundario", WS_VISIBLE, X + 620, Y, 100, 20, 0);
    fillRoleCombo(mk(0, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, X + 620,
                     Y + 22, 120, 300, IDC_ROLE_SECOND), true);
    mkButton(0, L"Guardar", X + 760, Y + 22, 100, 26, IDC_SAVE_PROFILE);

    mk(0, L"STATIC", L"Champion pool", WS_VISIBLE, X, Y + 66, 200, 20, 0);
    HWND lv = mk(0, WC_LISTVIEWW, L"", WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, X,
                 Y + 90, 520, 250, IDC_POOL_LIST);
    addListViewColumns(lv, {{L"Campeón", 200}, {L"Rol", 90}, {L"Nivel", 120}, {L"Partidas", 80}});
    g->poolIcons = ImageList_Create(28, 28, ILC_COLOR32, 16, 64);
    ListView_SetImageList(lv, g->poolIcons, LVSIL_SMALL);

    int rx = X + 545;
    mkButton(0, L"✨ Sugerir pool desde mi historial", rx, Y + 90, 300, 30, IDC_POOL_SUGGEST);
    HWND cb = mk(0, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, rx, Y + 132,
                 300, 380, IDC_POOL_CHAMP);
    (void)cb;                        // filled after Data Dragon loads
    fillRoleCombo(mk(0, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, rx, Y + 166,
                     145, 300, IDC_POOL_ROLE), false);
    HWND cbTier = mk(0, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, rx + 155,
                     Y + 166, 145, 300, IDC_POOL_TIER);
    for (auto* t : {L"main", L"cómodo", L"aprendiendo", L"no recomendar"})
        SendMessageW(cbTier, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(cbTier, CB_SETCURSEL, 1, 0);
    mk(0, L"STATIC", L"Partidas aprox.", WS_VISIBLE, rx, Y + 202, 110, 22, 0);
    mk(0, L"EDIT", L"0", WS_VISIBLE | WS_BORDER | ES_NUMBER, rx + 115, Y + 200, 60, 24,
       IDC_POOL_GAMES);
    mkButton(0, L"Añadir", rx, Y + 236, 145, 28, IDC_POOL_ADD);
    mkButton(0, L"Quitar", rx + 155, Y + 236, 145, 28, IDC_POOL_DEL);

    mkButton(0, L"⬇ Descargar del cliente (League)", X, Y + 360, 240, 32, IDC_FETCH_API);
    mkButton(0, L"Analizar pendientes", X + 250, Y + 360, 180, 32, IDC_ANALYZE);
    mkButton(0, L"Importar JSON…", X + 440, Y + 360, 150, 32, IDC_IMPORT_JSON);
    mk(0, L"STATIC",
       L"Con el cliente de League abierto no necesitas configurar nada: identidad, historial y "
       L"pool salen del propio cliente.",
       WS_VISIBLE, X, Y + 404, W, 40, 0);
    mk(0, L"STATIC", L"", WS_VISIBLE, X, Y + 448, W, 44, IDC_PROFILE_STATUS);

    // ---- 1: Partidas -------------------------------------------------------
    HWND ml = mk(1, WC_LISTVIEWW, L"", WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, X, Y,
                 W, 480, IDC_MATCH_LIST);
    addListViewColumns(ml, {{L"Campeón", 200}, {L"Rol", 100}, {L"Resultado", 100},
                            {L"Duración", 90}, {L"Parche", 80}, {L"Estado", 110}});
    g->matchIcons = ImageList_Create(28, 28, ILC_COLOR32, 32, 64);
    ListView_SetImageList(ml, g->matchIcons, LVSIL_SMALL);
    mkButton(1, L"Ver análisis", X, Y + 492, 140, 30, IDC_MATCH_OPEN);
    mkButton(1, L"Refrescar", X + 150, Y + 492, 120, 30, IDC_MATCH_REFRESH);

    // ---- 2: Post-match -----------------------------------------------------
    mkReport(2, X, Y, W, 440, IDC_POST_VIEW);
    mk(2, L"STATIC", L"¿El diagnóstico principal es correcto?", WS_VISIBLE, X, Y + 452, 260, 24,
       0);
    mkButton(2, L"Correcto", X + 270, Y + 448, 96, 28, IDC_FB_OK);
    mkButton(2, L"Parcial", X + 372, Y + 448, 96, 28, IDC_FB_PARTIAL);
    mkButton(2, L"Incorrecto", X + 474, Y + 448, 96, 28, IDC_FB_WRONG);
    mkButton(2, L"Falta contexto", X + 576, Y + 448, 120, 28, IDC_FB_CTX);
    mkButton(2, L"Aceptar misión sugerida", X, Y + 488, 210, 32, IDC_ACCEPT_MISSION);

    // ---- 3: Misión ---------------------------------------------------------
    mkReport(3, X, Y, 520, 440, IDC_MISSION_VIEW);
    mkButton(3, L"Sugerir misión", X, Y + 452, 150, 32, IDC_SUGGEST_MISSION);
    mkButton(3, L"Refrescar", X + 160, Y + 452, 120, 32, IDC_MISSION_REFRESH);
    mkButton(3, L"Aceptar sugerida", X + 290, Y + 452, 160, 32, IDC_ACCEPT_MISSION + 1);
    mk(3, L"STATIC", L"Árbol privado de habilidades", WS_VISIBLE, X + 545, Y, 300, 22, 0);
    HWND sl = mk(3, WC_LISTVIEWW, L"", WS_VISIBLE | WS_BORDER | LVS_REPORT, X + 545, Y + 26, 335,
                 414, IDC_SKILLS_LIST);
    addListViewColumns(sl, {{L"Dominio", 140}, {L"Estado", 100}, {L"Confianza", 90}});

    // ---- 4: Draft Lab ------------------------------------------------------
    mk(4, L"STATIC", L"Rol", WS_VISIBLE, X, Y, 60, 20, 0);
    fillRoleCombo(mk(4, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, X, Y + 20,
                     110, 300, IDC_DRAFT_ROLE), false);
    mk(4, L"STATIC", L"Tu campeón", WS_VISIBLE, X + 124, Y, 100, 20, 0);
    mk(4, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, X + 124, Y + 20, 170, 380,
       IDC_DRAFT_CHAMP);
    mkButton(4, L"Top 3", X + 310, Y + 20, 90, 26, IDC_DRAFT_GO);
    mkButton(4, L"Plan pregame", X + 408, Y + 20, 120, 26, IDC_DRAFT_PLAN);
    mkButton(4, L"Probar quiz", X + 536, Y + 20, 110, 26, IDC_DRAFT_QUIZ);

    mk(4, L"STATIC", L"Aliados", WS_VISIBLE, X, Y + 56, 70, 20, 0);
    for (int i = 0; i < 4; ++i)
        mk(4, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, X + 70 + i * 168,
           Y + 52, 160, 380, IDC_DRAFT_ALLY0 + i);
    mk(4, L"STATIC", L"Rivales", WS_VISIBLE, X, Y + 90, 70, 20, 0);
    for (int i = 0; i < 5; ++i)
        mk(4, L"COMBOBOX", L"", WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, X + 70 + i * 168,
           Y + 86, 160, 380, IDC_DRAFT_ENEMY0 + i);

    mkReport(4, X, Y + 126, W, 400, IDC_DRAFT_VIEW);
    rvSet(ctl(IDC_DRAFT_VIEW),
          {{RVKind::Title, L"Draft Lab"},
           {RVKind::Text, L"En champion select esta página se rellena sola desde el Agent."},
           {RVKind::Dim, L"Sin cliente: elige rol y campeones en los desplegables y pulsa "
                         L"Top 3 o Plan pregame."}});

    // ---- 5: Parche ---------------------------------------------------------
    mkReport(5, X, Y, W, 480, IDC_PATCH_VIEW);
    mkButton(5, L"Actualizar", X, Y + 492, 130, 30, IDC_PATCH_REFRESH);

    // ---- 6: Ajustes --------------------------------------------------------
    mk(6, L"BUTTON", L"Overlay en partida", WS_VISIBLE | BS_AUTOCHECKBOX, X, Y, 220, 26,
       IDC_SET_OVERLAY);
    mk(6, L"BUTTON", L"Lectura del cliente (LCU)", WS_VISIBLE | BS_AUTOCHECKBOX, X + 240, Y, 240,
       26, IDC_SET_LCU);
    mk(6, L"BUTTON", L"Grabación local (beta)", WS_VISIBLE | BS_AUTOCHECKBOX, X + 500, Y, 240, 26,
       IDC_SET_CAPTURE);
    mk(6, L"STATIC", L"Ruta del lockfile (vacío = autodetectar)", WS_VISIBLE, X, Y + 40, 280, 22,
       0);
    mk(6, L"EDIT", L"", WS_VISIBLE | WS_BORDER, X + 290, Y + 38, 380, 26, IDC_SET_LOCKFILE);
    mkButton(6, L"Guardar ajustes", X, Y + 78, 150, 30, IDC_SET_SAVE);
    mkButton(6, L"Log de auditoría", X + 160, Y + 78, 150, 30, IDC_SET_AUDIT);
    mkButton(6, L"Borrar TODOS los datos", X + 320, Y + 78, 200, 30, IDC_SET_WIPE);
    mkReport(6, X, Y + 124, W, 400, IDC_SET_VIEW);
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
        case 5: rvSet(ctl(IDC_PATCH_VIEW), rvPatch()); break;
        case 6: refreshSettings(); break;
    }
    InvalidateRect(g->hwnd, nullptr, TRUE);
}

// ------------------------------------------------------------------- wndproc

void paintChrome(HDC dc) {
    RECT rc;
    GetClientRect(g->hwnd, &rc);
    // Sidebar background.
    RECT sb{0, 0, kSidebarW, rc.bottom};
    FillRect(dc, &sb, theme::sidebarBrush());
    SetBkMode(dc, TRANSPARENT);

    SelectObject(dc, theme::h1());
    SetTextColor(dc, theme::kAccent);
    TextOutW(dc, 22, 22, L"RiftLoop", 8);
    SelectObject(dc, theme::tiny());
    SetTextColor(dc, theme::kDim);
    std::wstring patch = g->ddOk ? L"Parche " + w(g->dd.displayPatch()) + L" · " +
                                       w(g->dd.locale())
                                 : L"sin datos estáticos";
    TextOutW(dc, 24, 54, patch.c_str(), (int)patch.size());

    // State footer.
    SelectObject(dc, theme::small_());
    bool live = g->agentState != "sin agente";
    HBRUSH dot = CreateSolidBrush(live ? theme::kGood : theme::kDim);
    RECT dr{22, rc.bottom - 40, 32, rc.bottom - 30};
    FillRect(dc, &dr, dot);
    DeleteObject(dot);
    SetTextColor(dc, theme::kDim);
    std::wstring st = w(g->agentState);
    TextOutW(dc, 40, rc.bottom - 44, st.c_str(), (int)st.size());

    // Page header.
    SelectObject(dc, theme::h1());
    SetTextColor(dc, theme::kText);
    TextOutW(dc, kSidebarW + 28, 26, kPageNames[g->currentPage],
             (int)wcslen(kPageNames[g->currentPage]));
    SelectObject(dc, theme::small_());
    SetTextColor(dc, theme::kDim);
    TextOutW(dc, kSidebarW + 28, 62, kPageSubtitles[g->currentPage],
             (int)wcslen(kPageSubtitles[g->currentPage]));
}

void drawOwnerButton(DRAWITEMSTRUCT* dis) {
    int id = (int)dis->CtlID;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    wchar_t text[128];
    GetWindowTextW(dis->hwndItem, text, 128);
    SetBkMode(dc, TRANSPARENT);

    if (id >= IDC_NAV0 && id < IDC_NAV0 + kPages) {
        bool selected = g->currentPage == id - IDC_NAV0;
        HBRUSH bg = CreateSolidBrush(selected ? theme::kCard : theme::kSidebar);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        if (selected) {
            RECT bar{0, rc.top + 6, 4, rc.bottom - 6};
            HBRUSH ab = CreateSolidBrush(theme::kAccent);
            FillRect(dc, &bar, ab);
            DeleteObject(ab);
        }
        SelectObject(dc, theme::body());
        SetTextColor(dc, selected ? theme::kText : theme::kDim);
        RECT tr{rc.left + 22, rc.top, rc.right, rc.bottom};
        DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER);
        return;
    }
    // Segmented mode buttons.
    if (id == IDC_MODE_ESCALAR || id == IDC_MODE_APRENDER) {
        bool selected = (id == IDC_MODE_APRENDER) == g->modeAprender;
        HBRUSH bg = CreateSolidBrush(selected ? theme::kCardHi : theme::kCard);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        HBRUSH frame = CreateSolidBrush(selected ? theme::kAccent : theme::kBorder);
        FrameRect(dc, &rc, frame);
        DeleteObject(frame);
        SelectObject(dc, theme::small_());
        SetTextColor(dc, selected ? theme::kAccent : theme::kDim);
        DrawTextW(dc, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        return;
    }
    // Regular action buttons.
    bool danger = id == IDC_SET_WIPE;
    HBRUSH bg = CreateSolidBrush(theme::kCardHi);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HBRUSH frame = CreateSolidBrush(danger ? theme::kDanger : theme::kBorder);
    FrameRect(dc, &rc, frame);
    DeleteObject(frame);
    SelectObject(dc, theme::small_());
    SetTextColor(dc, danger ? theme::kDanger : theme::kAccent);
    DrawTextW(dc, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    if (dis->itemState & ODS_SELECTED) {
        HBRUSH sel = CreateSolidBrush(theme::kAccent);
        FrameRect(dc, &rc, sel);
        DeleteObject(sel);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            paintChrome(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, theme::bgBrush());
            return 1;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor((HDC)wp, theme::kText);
            SetBkColor((HDC)wp, theme::kCard);
            return (LRESULT)theme::cardBrush();
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetTextColor((HDC)wp, theme::kDim);
            SetBkColor((HDC)wp, theme::kBg);
            return (LRESULT)theme::bgBrush();
        case WM_DRAWITEM:
            drawOwnerButton((DRAWITEMSTRUCT*)lp);
            return TRUE;
        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lp;
            if (hdr->idFrom == IDC_MATCH_LIST && hdr->code == NM_DBLCLK) {
                int sel = ListView_GetNextItem(ctl(IDC_MATCH_LIST), -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)g->matchRowIds.size())
                    showAnalysis(g->matchRowIds[sel]);
                return 0;
            }
            if (hdr->code == NM_CUSTOMDRAW &&
                (hdr->idFrom == IDC_MATCH_LIST || hdr->idFrom == IDC_POOL_LIST ||
                 hdr->idFrom == IDC_SKILLS_LIST)) {
                auto* cd = (NMLVCUSTOMDRAW*)lp;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    cd->clrText = theme::kText;
                    cd->clrTextBk = theme::kCard;
                    return CDRF_DODEFAULT;
                }
            }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id >= IDC_NAV0 && id < IDC_NAV0 + kPages) {
                switchPage(id - IDC_NAV0);
                for (int i = 0; i < kPages; ++i)
                    InvalidateRect(ctl(IDC_NAV0 + i), nullptr, TRUE);
                return 0;
            }
            switch (id) {
                case IDC_MODE_ESCALAR:
                case IDC_MODE_APRENDER:
                    g->modeAprender = id == IDC_MODE_APRENDER;
                    InvalidateRect(ctl(IDC_MODE_ESCALAR), nullptr, TRUE);
                    InvalidateRect(ctl(IDC_MODE_APRENDER), nullptr, TRUE);
                    break;
                case IDC_SAVE_PROFILE: saveProfileFromUi(); break;
                case IDC_POOL_ADD: addPoolEntry(); break;
                case IDC_POOL_DEL: removePoolEntry(); break;
                case IDC_POOL_SUGGEST: suggestPoolFromHistory(); break;
                case IDC_IMPORT_JSON: importJsonFiles(); break;
                case IDC_FETCH_API: runFetch(); break;
                case IDC_ANALYZE: runAnalysis(); break;
                case IDC_MATCH_REFRESH: refreshMatchList(); break;
                case IDC_MATCH_OPEN: {
                    int sel = ListView_GetNextItem(ctl(IDC_MATCH_LIST), -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < (int)g->matchRowIds.size())
                        showAnalysis(g->matchRowIds[sel]);
                    break;
                }
                case IDC_FB_OK: giveFeedback("correcto"); break;
                case IDC_FB_PARTIAL: giveFeedback("parcial"); break;
                case IDC_FB_WRONG: giveFeedback("incorrecto"); break;
                case IDC_FB_CTX: giveFeedback("falta_contexto"); break;
                case IDC_ACCEPT_MISSION:
                case IDC_ACCEPT_MISSION + 1: acceptSuggestedMission(); break;
                case IDC_SUGGEST_MISSION: {
                    if (auto m = suggestMission(*g->db)) {
                        refreshMission();
                    } else {
                        MessageBoxW(hwnd,
                                    L"Aún no hay un patrón con oportunidades suficientes "
                                    L"(analiza varias partidas primero).",
                                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }
                case IDC_MISSION_REFRESH: refreshMission(); break;
                case IDC_DRAFT_GO: draftRecommend(); break;
                case IDC_DRAFT_PLAN: draftPlan(); break;
                case IDC_DRAFT_QUIZ: openQuizWindow(); break;
                case IDC_PATCH_REFRESH: rvSet(ctl(IDC_PATCH_VIEW), rvPatch()); break;
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
                case IDC_SET_AUDIT: {
                    std::vector<RVItem> v{{RVKind::Title, L"Log de auditoría"}};
                    for (auto& line : g->db->auditLog(60)) v.push_back({RVKind::Dim, w(line)});
                    if (v.size() == 1) v.push_back({RVKind::Text, L"(vacío)"});
                    rvSet(ctl(IDC_SET_VIEW), std::move(v));
                    break;
                }
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
        }
        case WM_APP_IPC:
            if (wp == 1) {
                EnableWindow(ctl(IDC_ANALYZE), TRUE);
                setText(IDC_PROFILE_STATUS, std::to_string((int)lp) + " partidas analizadas.");
                refreshMatchList();
                refreshMission();
                auto rows = g->db->listMatches(1);
                if ((int)lp > 0 && !rows.empty()) showAnalysis(rows[0].matchId);
            } else if (wp == 2) {
                EnableWindow(ctl(IDC_FETCH_API), TRUE);
                int imported = (int)lp;
                setText(IDC_PROFILE_STATUS, imported < 0
                    ? "No se pudo leer el historial: abre el cliente de League y reintenta."
                    : std::to_string(imported) +
                      " partidas nuevas del cliente. Pulsa 'Analizar pendientes'.");
                refreshMatchList();
                refreshProfileUi();
            } else {
                processIpcQueue();
            }
            return 0;
        case WM_APP_ICONS:
            // A batch of Data Dragon icons landed: repaint what shows them.
            for (int id : {IDC_POST_VIEW, IDC_DRAFT_VIEW, IDC_PATCH_VIEW, IDC_MISSION_VIEW,
                           IDC_SET_VIEW})
                if (HWND v = ctl(id)) InvalidateRect(v, nullptr, FALSE);
            if (g->currentPage == 0) refreshProfileUi();
            if (g->currentPage == 1) refreshMatchList();
            if (g->quizWnd) InvalidateRect(g->quizWnd, nullptr, TRUE);
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
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

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
        Config cfg = Config::load();
        std::string locale = resolveDataLocale(*app.db, nullptr, cfg.dataLocale);
        app.ddOk = app.dd.load(true, locale);
    }
    if (app.ddOk) {
        for (auto& id : app.dd.championIds())
            app.champList.push_back({w(app.dd.champion(id)->name), id});
        std::sort(app.champList.begin(), app.champList.end());
    }

    registerReportView(hInst);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RiftLoopDesktopWnd";
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    app.hwnd = CreateWindowW(wc.lpszClassName, L"RiftLoop",
                             (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME),
                             CW_USEDEFAULT, CW_USEDEFAULT, kWinW, kWinH, nullptr, nullptr, hInst,
                             nullptr);
    applyDarkTitleBar(app.hwnd);
    icons::init(app.hwnd);

    // Sidebar navigation.
    for (int i = 0; i < kPages; ++i) {
        HWND b = CreateWindowW(L"BUTTON", kPageNames[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
                               96 + i * 46, kSidebarW, 44, app.hwnd,
                               (HMENU)(INT_PTR)(IDC_NAV0 + i), hInst, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)theme::body(), TRUE);
    }

    buildPages();
    if (app.ddOk) {
        fillChampCombo(ctl(IDC_POOL_CHAMP));
        fillChampCombo(ctl(IDC_DRAFT_CHAMP));
        for (int i = 0; i < 4; ++i) fillChampCombo(ctl(IDC_DRAFT_ALLY0 + i));
        for (int i = 0; i < 5; ++i) fillChampCombo(ctl(IDC_DRAFT_ENEMY0 + i));
    }
    switchPage(0);
    ShowWindow(app.hwnd, SW_SHOW);

    app.client = std::make_unique<ipc::Client>(onIpc);
    app.client->start();

    if (app.db->loadProfile().pool.empty()) {
        MessageBoxW(app.hwnd,
                    L"Bienvenido a RiftLoop (iteración local).\n\n"
                    L"Lee el estado del cliente de League (solo lectura), analiza tus partidas y "
                    L"guarda todo en tu equipo. No escribe runas ni hechizos y no envía tus datos "
                    L"a ningún servidor propio.\n\n"
                    L"Con League abierto: pulsa \"Descargar del cliente\" y después "
                    L"\"✨ Sugerir pool desde mi historial\". Cero configuración manual.",
                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    icons::shutdown();
    return 0;
}
