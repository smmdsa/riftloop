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
#include "core/clipmaker.h"
#include "core/replays.h"
#include "core/videoframe.h"

#include "player.h"
#include "core/perkpages.h"
#include "core/missions.h"
#include "core/patchimpact.h"
#include "core/planner.h"
#include "core/podium.h"
#include "core/rank.h"
#include "core/rivalrank.h"
#include "core/recommend.h"
#include "core/serial.h"
#include "core/util.h"
#include "curveview.h"
#include "heatview.h"
#include "ui.h"

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>

#include <algorithm>
#include <map>
#include <filesystem>
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
    IDC_POOL_ADD, IDC_POOL_DEL, IDC_POOL_SUGGEST, IDC_SAVE_PROFILE, IDC_RANK_LABEL,
    IDC_IMPORT_JSON, IDC_FETCH_API, IDC_ANALYZE, IDC_PROFILE_STATUS,
    // matches
    IDC_MATCH_LIST = 1200, IDC_MATCH_OPEN, IDC_MATCH_REFRESH, IDC_MATCH_FETCH,
    IDC_RANKS_VIEW, IDC_RANKS_FETCH, IDC_RANKS_ONE, IDC_RANKS_FORCE,
    // postmatch
    IDC_POST_VIEW = 1300, IDC_FB_OK, IDC_FB_PARTIAL, IDC_FB_WRONG, IDC_FB_CTX,
    IDC_ACCEPT_MISSION,
    IDC_CLIP_LIST = 1320, IDC_CLIP_PLAYER, IDC_CLIP_STATUS, IDC_CLIP_PROGRESS,
    IDC_CLIP_PLAYPAUSE, IDC_CLIP_CLOSE, IDC_CLIP_FULL,
    // mission
    IDC_MISSION_VIEW = 1400, IDC_SUGGEST_MISSION, IDC_SKILLS_LIST, IDC_MISSION_REFRESH,
    // draft
    IDC_DRAFT_GO = 1500, IDC_DRAFT_PLAN, IDC_DRAFT_QUIZ,
    IDC_DRAFT_VIEW,
    IDC_APPLY_RUNES = 1540, IDC_UNDO_RUNES,
    // map
    IDC_HEAT_VIEW = 1560, IDC_HEAT_20, IDC_HEAT_50, IDC_HEAT_ROLE,
    // curves
    IDC_CURVE_VIEW = 1580, IDC_CURVE_PREV, IDC_CURVE_NEXT,
    // patch
    IDC_PATCH_VIEW = 1600, IDC_PATCH_REFRESH,
    // settings
    IDC_SET_OVERLAY = 1700, IDC_SET_LCU, IDC_SET_CAPTURE, IDC_SET_LOCKFILE, IDC_SET_SAVE,
    IDC_SET_WIPE, IDC_SET_AUDIT, IDC_SET_VIEW, IDC_SET_META, IDC_SET_CLIPS,
    IDC_OPEN_CLIPS, IDC_PRUNE_CLIPS, IDC_SET_KEEPFULL,
    IDC_MAKE_CLIPS, IDC_SET_RUNEWRITE,
    IDC_SET_APIKEY, IDC_SET_APIKEY_SAVE, IDC_SET_APIKEY_CLEAR, IDC_SET_APIKEY_STATE,
    // quiz window
    IDC_QUIZ_OPT0 = 1800,            // ..1803
    IDC_QUIZ_NEXT = 1810,
};

constexpr UINT WM_APP_IPC = WM_APP + 10;
constexpr UINT WM_APP_CLIPS = WM_APP + 11;      // wParam: 1 progress, 2 done
// PRD 13.4: full recordings are capped at 2 GB of local storage.
constexpr int64_t kRecordingQuotaBytes = 2LL * 1024 * 1024 * 1024;
constexpr int kPages = 9;
const wchar_t* kPageNames[kPages] = {L"Perfil", L"Partidas", L"Post-match", L"Misión",
                                     L"Draft Lab", L"Mapa", L"Curvas", L"Parche", L"Ajustes"};
const wchar_t* kPageSubtitles[kPages] = {
    L"Tu cuenta, tu champion pool y tus datos",
    L"Historial importado desde el cliente",
    L"Evidencia, confianza y feedback",
    L"Una misión activa, medida por oportunidades",
    L"Top 3 y plan pregame (se rellena solo en champion select)",
    L"Dónde mueres y dónde matas, sobre tus últimas partidas",
    L"Oro, CS y experiencia de una partida contra tu propia mediana",
    L"Impacto del parche en tu pool",
    L"Módulos, privacidad y auditoría"};

constexpr int kSidebarW = 200;
constexpr int kWinW = 1560, kWinH = 900;
// The draft view runs to the bottom edge of the window: the page had 266 px
// of unused height under it.
constexpr int kDraftViewH = 700;
// Right-hand column of the post-match page: player on top, clip cards below.
constexpr int kClipPanelW = 430;
const char* kRoles[5] = {"TOP", "JUNGLE", "MIDDLE", "BOTTOM", "UTILITY"};

struct App {
    HWND hwnd = nullptr;
    std::vector<HWND> pageControls[kPages];
    int currentPage = 0;
    int  heatMatches = 20;      // how many matches the heat map reads
    int  curveIndex = 0;        // which stored match the curve page shows
    bool heatRoleOnly = false;

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

    // Draft mirrored from the agent. Bans and the pickable set have no combo of
    // their own, so the Draft Lab keeps them here for a manual recompute.
    std::array<DraftPick, 5> draftAllySeats{}, draftEnemySeats{};
    int draftLocalSeat = -1;
    std::string draftRole, draftMyChampion;
    std::vector<std::string> draftAllyBans, draftEnemyBans, draftUnknownBans;
    std::vector<std::string> draftPickable;

    // Last rune page shown. Applying writes exactly this, never something
    // recomputed behind the user's back.
    RunePage lastRunePage;
    std::string lastPlanChampion, lastPlanRole;

    std::string currentMatchId;

    // clip playlist and player
    HWND playerWnd = nullptr;        // kept directly: the player is not a dialog control
    std::vector<HBITMAP> clipThumbBitmaps;
    std::vector<std::string> clipRowFiles;
    std::unique_ptr<VideoPlayer> player;
    std::mutex clipMutex;
    std::string clipStatus;
    int clipStep = 0, clipTotal = 0;

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

// Folder of the running executable, to launch the sibling processes.
std::wstring exeDirW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path = buf;
    return path.substr(0, path.find_last_of(L'\\'));
}
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

// The draft at a glance: one row of bans, one row of picks, our side facing
// theirs. It replaces nine dropdowns and a comma-separated list of names.
//
// The colour means "your team" and "the enemy team", not the side of the map.
// The user asked for the real side on 2026-09-07, and the client does not
// expose it in champion select: TASK-0017 measures whether it can be derived.
// Until it answers, these two lines are the only thing that has to change.
constexpr COLORREF kOurSide = theme::kAccent;
constexpr COLORREF kTheirSide = theme::kDanger;

std::vector<RVSeat> seatsToRv(const std::array<DraftPick, 5>& seats, int userSeat) {
    std::vector<RVSeat> out;
    for (int i = 0; i < (int)seats.size(); ++i) {
        RVSeat s;
        s.champion = seats[i].champion;
        s.iconUrl = s.champion.empty() ? "" : champIconUrl(s.champion);
        s.hover = seats[i].state == PickState::Hover;
        s.isUser = (i == userSeat);
        if (s.isUser) s.label = L"tú";
        else if (s.hover) s.label = L"eligiendo";
        out.push_back(s);
    }
    return out;
}

std::vector<RVSeat> bansToRv(const std::vector<std::string>& bans) {
    std::vector<RVSeat> out;
    for (auto& b : bans) {
        RVSeat s;
        s.champion = b;
        s.iconUrl = champIconUrl(b);
        s.banned = true;
        out.push_back(s);
    }
    return out;
}

// Nothing to show yet. The page states who fills it, and asks for nothing:
// the user has no control here to complete (TASK-0010).
// A sample draft, for looking at the Draft Lab without queueing for a game.
// Started with --demo-draft. It is a development flag, not a manual mode: the
// user never types a champion here.
void loadDemoDraft() {
    auto seat = [](const char* id, PickState st) { return DraftPick{id, st}; };
    g->draftRole = "BOTTOM";
    g->draftMyChampion = "Jinx";
    g->draftLocalSeat = 3;
    g->draftAllySeats = {seat("KSante", PickState::Locked), seat("LeeSin", PickState::Locked),
                         seat("Ahri", PickState::Hover), seat("Jinx", PickState::Locked),
                         DraftPick{}};
    g->draftEnemySeats = {seat("Camille", PickState::Locked), seat("RekSai", PickState::Locked),
                          DraftPick{}, seat("Caitlyn", PickState::Hover), DraftPick{}};
    g->draftAllyBans = {"Rell", "MasterYi", "Illaoi", "Zed", "Yasuo"};
    g->draftEnemyBans = {"Vi", "Sett", "Akali"};
    g->draftUnknownBans = {"Kaisa"};
}

std::vector<RVItem> rvDraftIdle() {
    return {{RVKind::Title, L"Draft Lab"},
            {RVKind::Text, L"Esta página se rellena sola al entrar en champion select."},
            {RVKind::Dim, L"Los picks aparecen uno a uno según se eligen, y los baneos con "
                          L"el equipo que los hizo. No hay nada que completar a mano."}};
}

void rvAddDraftStrips(std::vector<RVItem>& v) {
    bool anyPick = g->draftLocalSeat >= 0;
    for (auto* side : {&g->draftAllySeats, &g->draftEnemySeats})
        for (auto& s : *side)
            if (!s.champion.empty()) anyPick = true;
    bool anyBan = !g->draftAllyBans.empty() || !g->draftEnemyBans.empty() ||
                  !g->draftUnknownBans.empty();
    if (!anyPick && !anyBan) return;

    RVItem bans{RVKind::TeamStrip};
    bans.leftSeats = bansToRv(g->draftAllyBans);
    bans.rightSeats = bansToRv(g->draftEnemyBans);
    bans.leftColor = kOurSide;
    bans.rightColor = kTheirSide;
    bans.compact = true;
    bans.text = g->draftUnknownBans.empty()
                    ? L"BANEOS"
                    : L"BANEOS · " + std::to_wstring(g->draftUnknownBans.size()) +
                          L" sin equipo conocido";
    v.push_back(bans);

    RVItem picks{RVKind::TeamStrip};
    picks.leftSeats = seatsToRv(g->draftAllySeats, g->draftLocalSeat);
    picks.rightSeats = seatsToRv(g->draftEnemySeats, -1);
    picks.leftColor = kOurSide;
    picks.rightColor = kTheirSide;
    picks.text = L"PICKS";
    v.push_back(picks);
    v.push_back({RVKind::Spacer, L""});
}

std::vector<RVItem> rvTop3(const Top3& t) {
    std::vector<RVItem> v;
    v.push_back({RVKind::Title, L"Top 3 para esta partida"});
    rvAddDraftStrips(v);
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

// One rune page: header, what it goes for, the six perks with icons, the
// shards and the reasons. Both pages of a plan use it, so the user compares
// like with like. badge is optional and rides under the header.
void rvAddRunePage(std::vector<RVItem>& v, const std::wstring& label, const RunePage& p,
                   const RVItem* badge) {
    v.push_back({RVKind::Section, label + L" — " + w(g->dd.styleName(p.primaryStyle)) + L" + " +
                                      w(g->dd.styleName(p.subStyle))});
    if (badge) v.push_back(*badge);
    // The core writes this line. The UI never composes it (PRD 12.4).
    if (!p.intent.empty()) v.push_back({RVKind::Text, w(p.intent)});
    rvAddPerkRow(v, p.perks[0], L"piedra angular", 0);
    for (int i = 1; i <= 3; ++i) rvAddPerkRow(v, p.perks[i], L"", 12);
    rvAddPerkRow(v, p.perks[4], L"secundaria", 12);
    rvAddPerkRow(v, p.perks[5], L"secundaria", 12);
    v.push_back({RVKind::Dim, L"Fragmentos: " + w(g->dd.shardName(p.perks[6])) + L" · " +
                                  w(g->dd.shardName(p.perks[7])) + L" · " +
                                  w(g->dd.shardName(p.perks[8]))});
    for (auto& r : p.reasons) v.push_back({RVKind::Dim, L"• " + w(r)});
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
    rvAddDraftStrips(v);

    if (g->ddOk && rp.main.perks.size() >= 9) {
        RVItem badge{RVKind::Badge, confBadge(rp.confidence)};
        badge.color = confColor(rp.confidence);
        rvAddRunePage(v, L"Runas", rp.main, &badge);
        // RF-RUN-001: say whether the draft can still change this page.
        v.push_back({RVKind::Dim, rp.draftClosed
            ? std::wstring(L"Plan definitivo: los 10 campeones estan bloqueados")
            : L"Plan provisional: faltan " + std::to_wstring(rp.missingPicks) +
                  L" picks; se rehace al cerrarse el draft"});
        // The alternative gets the same shape as the main page. Drawn smaller,
        // the user cannot compare the two and the choice is not a choice.
        if (rp.situational && rp.situational->perks.size() >= 9) {
            v.push_back({RVKind::Spacer, L""});
            rvAddRunePage(v, L"Alternativa", *rp.situational, nullptr);
        } else if (!rp.noAlternativeReason.empty()) {
            v.push_back({RVKind::Dim, L"Sin alternativa: " + w(rp.noAlternativeReason)});
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

void refreshClipList();

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
            // The clip that shows this exact moment. Clicking it opens the file
            // so the text can be checked against the video (PRD 9.11 evidence).
            if (!ev.clipFile.empty()) {
                RVItem clip{RVKind::Dim, L"▶ Ver este momento en video"};
                clip.indent = 24;
                clip.color = theme::kAccent;
                clip.action = "clip:" + ev.clipFile;
                v.push_back(clip);
            }
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

// Full-game recordings are named by timestamp (20260904_011134.mp4). Evidence
// clips carry a match id instead, and must survive any cleanup: they are the
// proof behind a diagnosis.
bool isFullRecording(const std::string& name) {
    if (name.size() < 16 || name[8] != '_') return false;
    for (int i = 0; i < 8; ++i)
        if (!isdigit((unsigned char)name[i])) return false;
    return true;
}

struct RecordingsInfo {
    int      files = 0;
    int64_t  bytes = 0;
    int      clipFiles = 0;
    int64_t  clipBytes = 0;
};

RecordingsInfo recordingsInfo() {
    RecordingsInfo info;
    std::error_code ec;
    auto dir = util::clipsDir();
    if (!std::filesystem::exists(dir, ec)) return info;
    // The full recordings sit at the top level. The cut clips live one folder
    // per match under it, so the walk goes down to reach them. Only .mp4 counts:
    // a flat count also caught the .mp4.json sidecars and reported them as
    // recordings.
    for (auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != ".mp4") continue;
        if (e.path().parent_path() == dir && isFullRecording(e.path().filename().string())) {
            ++info.files;
            info.bytes += (int64_t)e.file_size(ec);
        } else {
            ++info.clipFiles;
            info.clipBytes += (int64_t)e.file_size(ec);
        }
    }
    return info;
}

std::wstring humanSize(int64_t bytes) {
    double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
    wchar_t buf[32];
    if (gb >= 1.0) swprintf_s(buf, L"%.1f GB", gb);
    else swprintf_s(buf, L"%.0f MB", (double)bytes / (1024.0 * 1024.0));
    return buf;
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
    v.push_back({RVKind::Text, L"Versión: " + w(kAppVersion) + L" · compilado " +
                                   w(appBuildStamp())});
    v.push_back({RVKind::Text, L"Carpeta local: " + util::widen(util::dataDir().string())});
    std::string last = g->db->getKv("last_meta_refresh");
    v.push_back({RVKind::Text, L"Muestra local de builds: " + w(std::to_string(g->db->buildCount())) +
                                   L" (ultimo refresco: " +
                                   (last.empty() ? std::wstring(L"nunca") : w(last)) + L")"});
    v.push_back({RVKind::Dim, L"Runas y builds salen de tus propias partidas: cada partida "
                              L"importada aporta las 10 páginas y las 10 builds de esa "
                              L"partida. No se consulta ningún sitio de terceros."});
    v.push_back({RVKind::Section, L"Privacidad"});
    v.push_back({RVKind::Dim, L"Todo se guarda en tu equipo (SQLite local). Este build no "
                              L"escribe nada en el cliente de League y no envía datos a "
                              L"servidores propios."});
    v.push_back({RVKind::Dim, L"Grabación: " + std::wstring(cfg.captureEnabled
                                                                ? L"activada (beta, MP4 local)"
                                                                : L"desactivada (opt-in)")});
    RecordingsInfo rec = recordingsInfo();
    std::wstring recLine = L"Grabaciones: " + std::to_wstring(rec.files) + L" archivos · " +
                           humanSize(rec.bytes) + L" (límite recomendado 2 GB)";
    if (rec.clipFiles > 0)
        recLine += L" · " + std::to_wstring(rec.clipFiles) + L" clips de evidencia";
    RVItem recItem{RVKind::Text, recLine};
    if (rec.bytes > kRecordingQuotaBytes) recItem.color = theme::kWarn;
    v.push_back(recItem);
    v.push_back({RVKind::Dim, cfg.keepFullRecording
        ? L"Al analizar cada partida se cortan los clips de evidencia y se conserva también "
          L"la grabación completa (~1 GB por hora)."
        : L"Al analizar cada partida se cortan los clips de evidencia y se borra la "
          L"grabación completa. Si algún corte falla, la grabación se conserva."});
    if (rec.bytes > kRecordingQuotaBytes)
        v.push_back({RVKind::Dim, L"Superas el límite. \"Liberar espacio\" borra las grabaciones "
                                  L"completas más antiguas hasta bajar de 2 GB. Los clips de "
                                  L"evidencia no se tocan."});
    return v;
}

// ------------------------------------------------------------- page refresh

// The match highlighted in the list. Empty when nothing is selected.
std::string selectedMatchId();

std::string selectedMatchId() {
    int sel = ListView_GetNextItem(ctl(IDC_MATCH_LIST), -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)g->matchRowIds.size()) return {};
    return g->matchRowIds[sel];
}

// The ten ranks of one match already played, with how many are still unread.
// The average is only shown when the core offers it (PRD 3.3: evidence, or
// silence).
std::vector<RVItem> rvMatchRanks(const std::string& matchId) {
    std::vector<RVItem> v;
    if (matchId.empty()) {
        v.push_back({RVKind::Dim, L"Elige una partida de la lista para ver los rangos."});
        return v;
    }
    auto match = parseMatch(g->db->matchJson(matchId));
    if (!match) {
        v.push_back({RVKind::Dim, L"Esa partida no se puede leer."});
        return v;
    }
    MatchRankView rv = storedMatchRanks(*g->db, matchId);
    std::string userPuuid = resolveUserPuuid(*g->db);

    // The podium of the ten. A bare percentage did not read at a glance, so
    // the row carries a place and a title and the tip carries the arithmetic
    // (core/podium.h). It scores ONE game and is never stored (PRD 7).
    MatchPodium podium = buildPodium(*match);

    std::wstring head = rv.averageTier.empty()
                      ? L"La partida, jugador a jugador"
                      : L"La partida, jugador a jugador  ·  promedio " + w(rv.averageTier) +
                        L" " + w(rv.averageDivision);
    v.push_back({RVKind::Section, head});

    for (int side = 0; side < 2; ++side) {
        int teamId = side == 0 ? 100 : 200;
        bool won = false;
        for (auto& p : match->participants)
            if (p.teamId == teamId) won = p.win;
        RVItem header;
        header.kind = RVKind::Dim;
        header.text = (side == 0 ? L"Equipo azul · " : L"Equipo rojo · ") +
                      std::wstring(won ? L"victoria" : L"derrota");
        header.color = won ? theme::kGood : theme::kDanger;
        v.push_back(std::move(header));

        for (auto& p : match->participants) {
            if (p.teamId != teamId) continue;
            const RivalRank* r = rv.byParticipant(p.participantId);
            RVItem it;
            it.kind = RVKind::PlayerRow;
            it.iconKind = "champ";
            it.iconId = p.championName;
            it.iconUrl = g->ddOk ? g->dd.championIconUrl(p.championName) : "";
            bool isUser = !userPuuid.empty() && p.puuid == userPuuid;
            it.text = w(champDisplay(p.championName)) +
                      (p.position.empty() ? L"" : L" · " + w(p.position)) +
                      (isUser ? L"  (tú)" : L"");
            it.kda = w(std::to_string(p.kills) + " / " + std::to_string(p.deaths) + " / " +
                       std::to_string(p.assists));
            std::string gold = std::to_string(p.goldEarned / 1000) + "." +
                               std::to_string((p.goldEarned % 1000) / 100) + "k";
            it.farm = w("niv " + std::to_string(p.champLevel) + " · " +
                        std::to_string(p.totalCs) + " CS · " + gold);
            it.color = isUser ? theme::kAccent
                              : (teamId == 100 ? RGB(58, 110, 165) : RGB(150, 62, 62));

            // Spells, then the two rune styles, then the six items. The gap
            // separates what the player chose before the game from what they
            // built during it.
            for (int spell : p.summonerSpells) {
                RVChip c;
                c.kind = "spell";
                // The payload carries the numeric key. summonerNameByKey gives
                // the localized name, which is not what the CDN path uses.
                if (spell > 0 && g->ddOk) {
                    c.url = g->dd.spellIconUrlByKey(spell);
                    if (!c.url.empty()) c.id = std::to_string(spell);
                }
                it.chips.push_back(std::move(c));
            }
            for (int style : {p.perkPrimaryStyle, p.perkSubStyle}) {
                RVChip c;
                c.kind = "perk";
                if (style > 0 && g->ddOk) {
                    c.id = std::to_string(style);
                    c.url = g->dd.perkIconUrl(style);
                }
                it.chips.push_back(std::move(c));
            }
            if (!p.perks.empty() && g->ddOk) {
                RVChip key;   // the keystone, the one rune that shapes the build
                key.kind = "perk";
                key.id = std::to_string(p.perks[0]);
                key.url = g->dd.perkIconUrl(p.perks[0]);
                it.chips.push_back(std::move(key));
            }
            it.chipGap = (int)it.chips.size();
            for (int item : p.finalItems) {
                RVChip c;
                c.kind = "item";
                if (item > 0 && g->ddOk) {
                    c.id = std::to_string(item);
                    c.url = g->dd.itemIconUrl(item);
                }
                it.chips.push_back(std::move(c));
            }

            if (const PodiumEntry* e = podium.byParticipant(p.participantId)) {
                it.place = std::to_wstring(e->rank) + L"º";
                it.title2 = w(e->title);
                if (r) it.right = w(r->display());

                // The tip: every factor, its numbers, and the points it gave.
                it.tipTitle = std::to_wstring(e->rank) + L"º de 10 · " +
                              w(champDisplay(p.championName));
                it.tipSubtitle = e->title.empty()
                               ? L"Puntuación de la partida: " + std::to_wstring(e->score) +
                                 L" sobre 100"
                               : w(e->title) + L" · " + w(e->titleWhy);
                for (const auto& f : e->factors) {
                    TipLine line;
                    line.label = w(f.label);
                    line.value = std::to_wstring(f.points) + L" / " + std::to_wstring(f.weight);
                    line.bar = f.weight > 0 ? f.points * 100 / f.weight : 0;
                    it.tipLines.push_back(std::move(line));
                    TipLine detail;
                    detail.label = L"      " + w(f.detail);
                    it.tipLines.push_back(std::move(detail));
                }
                TipLine total;
                total.label = L"Total";
                total.value = std::to_wstring(e->score) + L" / 100";
                total.bar = e->score;
                it.tipLines.push_back(std::move(total));
                it.tipFooter = L"Mide esta partida, no al jugador. No se guarda y no se "
                               L"compara con otras partidas.";
            }
            v.push_back(std::move(it));
        }
        if (side == 0) v.push_back({RVKind::Spacer});
    }

    v.push_back({RVKind::Dim,
                 L"Pasa el ratón por una fila para ver de dónde sale su puesto."});
    if (!rv.note.empty()) v.push_back({RVKind::Dim, w(rv.note)});
    v.push_back({RVKind::Dim,
                 L"El rango sólo se lee de partidas ya jugadas. En champion select los "
                 L"rivales están anonimizados y esta consulta no existe (PRD 17.2)."});
    return v;
}

void refreshMatchList() {
    HWND lv = ctl(IDC_MATCH_LIST);
    // Remember WHICH match was selected, not which row. DeleteAllItems drops
    // the selection, and a batch of icons lands here at any moment: without
    // this the reader is thrown back to the newest game mid-read.
    std::string wasSelected = selectedMatchId();
    std::string userPuuid = resolveUserPuuid(*g->db);
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
        // KDA, cs and gold come from the stored match, not from a new query.
        // A row that only names the champion says nothing about the game.
        std::string kda = "-", cs = "-", gold = "-";
        if (auto m = parseMatch(g->db->matchJson(r.matchId))) {
            const Participant* me = userPuuid.empty() ? nullptr : m->byPuuid(userPuuid);
            if (me) {
                kda = std::to_string(me->kills) + " / " + std::to_string(me->deaths) +
                      " / " + std::to_string(me->assists);
                cs = std::to_string(me->totalCs);
                gold = std::to_string(me->goldEarned / 1000) + "." +
                       std::to_string((me->goldEarned % 1000) / 100) + "k";
            }
        }
        set(3, kda);
        set(4, cs);
        set(5, gold);
        set(6, std::to_string(r.durationSec / 60) + " min");
        set(7, r.patch);
        set(8, r.analyzed ? "analizada" : "pendiente");
        ++i;
    }
    if (i == 0) return;
    // Put the mark back on the same match. Only a first visit, or a match that
    // is gone, falls back to the newest row: with nothing selected the rank
    // buttons have no match to work on.
    int row = 0;
    if (!wasSelected.empty()) {
        for (size_t k = 0; k < g->matchRowIds.size(); ++k)
            if (g->matchRowIds[k] == wasSelected) { row = (int)k; break; }
    }
    ListView_SetItemState(lv, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(lv, row, FALSE);
}

void refreshProfileUi() {
    Profile p = g->db->loadProfile();

    // The rank comes from the client when it is open, and from what was stored
    // when it is not. It always says which of the two it is (TASK-0024).
    Lcu rankLcu;
    rankLcu.connect(Config::load().leagueLockfilePath);
    RankView rank = currentRankView(*g->db, rankLcu.connected() ? &rankLcu : nullptr);
    setText(IDC_RANK_LABEL, rank.known ? rank.rank.display() + "   " + rank.note : rank.note);

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
    refreshClipList();
    switchPage(2);
}

void refreshSettings() {
    Config cfg = Config::load();
    CheckDlgButton(g->hwnd, IDC_SET_OVERLAY, cfg.overlayEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_LCU, cfg.lcuReadEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_CAPTURE, cfg.captureEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_CLIPS, cfg.clipsEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_RUNEWRITE, cfg.runeWriteEnabled ? BST_CHECKED : 0);
    CheckDlgButton(g->hwnd, IDC_SET_KEEPFULL, cfg.keepFullRecording ? BST_CHECKED : 0);
    setText(IDC_SET_LOCKFILE, cfg.leagueLockfilePath);

    // The key itself never goes back into the box: it is shown as a state, not
    // as a value, so a screenshot of this page leaks nothing.
    std::string key = Config::loadApiKey();
    setText(IDC_SET_APIKEY_STATE,
            key.empty() ? "Sin clave. Sin ella el rango de los rivales no se puede leer; "
                          "todo lo demas funciona igual."
                        : "Clave guardada y protegida con DPAPI (" +
                          key.substr(0, 9) + "...). Solo tu cuenta de Windows la lee.");
    setText(IDC_SET_APIKEY, "");
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
    // Client history + analysis in one click (also covers Alt+F4 games).
    setText(IDC_PROFILE_STATUS, "Buscando partidas nuevas en el cliente de League...");
    EnableWindow(ctl(IDC_FETCH_API), FALSE);
    EnableWindow(ctl(IDC_MATCH_FETCH), FALSE);
    std::thread([] {
        int imported = -1;
        int analyzed = 0;
        try {
            Db db;
            Config cfg = Config::load();
            Lcu lcu;
            if (lcu.connect(cfg.leagueLockfilePath)) {
                auto r = importFromClient(db, lcu, g->ddOk ? &g->dd : nullptr,
                                          cfg.matchImportCount);
                imported = r.error.empty() ? r.imported : -1;
            }
            analyzed = analyzePending(db, g->ddOk ? &g->dd : nullptr);
        } catch (...) {}
        PostMessageW(g->hwnd, WM_APP_IPC, 2, MAKELPARAM((WORD)(imported + 1), (WORD)analyzed));
    }).detach();
}

DraftContext draftContextFromUi() {
    DraftContext ctx;
    ctx.role = g->draftRole;
    ctx.allySeats = g->draftAllySeats;
    ctx.enemySeats = g->draftEnemySeats;
    ctx.localSeat = g->draftLocalSeat;
    ctx.allyBans = g->draftAllyBans;
    ctx.enemyBans = g->draftEnemyBans;
    ctx.unknownBans = g->draftUnknownBans;
    // The pickable set is only true while the client is in champion select.
    if (g->agentState == "ChampSelect") ctx.ownedOrPickable = g->draftPickable;
    ctx.patch = g->ddOk ? g->dd.version() : "";
    return ctx;
}

// Mirrors the live champion select into the Draft Lab controls. The user can
// still edit them; the next draft change overwrites the edit.
// Reads one side of the draft. A seat keeps its index, so an empty seat stays
// empty instead of pulling the next champion into its place.
std::array<DraftPick, 5> seatsFromJson(const json& d, const char* key) {
    std::array<DraftPick, 5> out{};
    if (!d.contains(key) || !d[key].is_array()) return out;
    const json& arr = d[key];
    for (size_t i = 0; i < arr.size() && i < out.size(); ++i) {
        std::string state = arr[i].value("state", "empty");
        out[i].champion = arr[i].value("champion", "");
        out[i].state = state == "locked"  ? PickState::Locked
                       : state == "hover" ? PickState::Hover
                                          : PickState::Empty;
    }
    return out;
}

void applyDraftView(const json& d) {
    g->draftRole = d.value("role", "");
    g->draftMyChampion = d.value("myChampion", "");
    g->draftAllySeats = seatsFromJson(d, "allySeats");
    g->draftEnemySeats = seatsFromJson(d, "enemySeats");
    g->draftLocalSeat = d.value("localSeat", -1);

    using Names = std::vector<std::string>;
    g->draftAllyBans = d.value("bansAllies", Names{});
    g->draftEnemyBans = d.value("bansEnemies", Names{});
    g->draftUnknownBans = d.value("bansUnknown", Names{});
    // An agent that still sends the flat "bans" field names no side. Keep the
    // bans instead of dropping them, and claim no side for them.
    if (!d.contains("bansAllies") && d.contains("bans"))
        g->draftUnknownBans = d.value("bans", Names{});
    g->draftPickable = d.value("pickable", Names{});

}

void clearDraftView() {
    g->draftAllySeats = {};
    g->draftEnemySeats = {};
    g->draftLocalSeat = -1;
    g->draftRole.clear();
    g->draftMyChampion.clear();
    g->draftAllyBans.clear();
    g->draftEnemyBans.clear();
    g->draftUnknownBans.clear();
    g->draftPickable.clear();
}

// Rebuilds the local meta sample: a larger history import, the rune backfill
// for older matches, then the builds table. Runs off the UI thread.
void refreshMetaSample() {
    EnableWindow(ctl(IDC_SET_META), FALSE);
    setText(IDC_SET_META, "Actualizando...");
    std::thread([] {
        int rows = -1;
        try {
            Db db;
            Config cfg = Config::load();
            Lcu lcu;
            if (lcu.connect(cfg.leagueLockfilePath)) {
                importFromClient(db, lcu, g->ddOk ? &g->dd : nullptr, cfg.metaImportCount);
                refetchRunePages(db, lcu, g->ddOk ? &g->dd : nullptr);
            }
            rows = db.rebuildBuilds(g->ddOk ? &g->dd : nullptr);
            db.setKv("last_meta_refresh", util::todayLocal());
        } catch (...) {}
        PostMessageW(g->hwnd, WM_APP_IPC, 3, (LPARAM)rows);
    }).detach();
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
    std::string champ = g->draftMyChampion;
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
    RunePlan rp = planRunes(*g->db, g->dd, pi);
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
    g->lastRunePage = rp.main;
    g->lastPlanChampion = pi.champion;
    g->lastPlanRole = pi.role;
    rvSet(ctl(IDC_DRAFT_VIEW), rvPlan(pi.champion, pi.role, rp, sp, ip, g->quiz));
}

// Writes the shown rune page to the client. RF-RUN-003: one human click, a
// diff first, and never a personal page. Nothing here runs on its own.
void applyRunesToClient() {
    if (!Config::load().runeWriteEnabled) {
        MessageBoxW(g->hwnd,
                    L"Aplicar runas al cliente está desactivado.\n\n"
                    L"Actívalo en Ajustes. Aunque lo actives, RiftLoop nunca aplica nada solo: "
                    L"cada escritura necesita este botón.",
                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g->lastRunePage.perks.size() < 9) {
        MessageBoxW(g->hwnd, L"Genera antes un plan pregame para tener una página que aplicar.",
                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Config cfg = Config::load();
    Lcu lcu;
    if (!lcu.connect(cfg.leagueLockfilePath)) {
        MessageBoxW(g->hwnd, L"El cliente de League no está abierto.", L"RiftLoop",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    std::string pageName = "RiftLoop: " + champDisplay(g->lastPlanChampion) +
                           (g->lastPlanRole.empty() ? "" : " " + g->lastPlanRole);
    auto pages = parsePerkPages(lcu.getRaw("/lol-perks/v1/pages"));
    auto inv = parsePerkInventory(lcu.getRaw("/lol-perks/v1/inventory"));
    PageWritePlan plan = planPageWrite(pages, inv, g->lastRunePage, pageName,
                                       g->db->getKv("rune_write_signature"), g->dd);

    if (plan.action == WriteAction::NoChange) {
        setText(IDC_PROFILE_STATUS, "La página ya estaba aplicada; no se tocó nada.");
        return;
    }
    bool force = false;
    if (plan.action == WriteAction::Blocked) {
        if (!plan.userEdited) {
            MessageBoxW(g->hwnd, w(plan.blockedReason).c_str(), L"No se aplica",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        if (MessageBoxW(g->hwnd,
                        (w(plan.blockedReason) +
                         L"\n\n¿Sobrescribo tu edición con la página de RiftLoop?").c_str(),
                        L"Página editada a mano", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        force = true;
    } else {
        // Show the diff before touching anything (RF-RUN-003).
        std::wstring text = (plan.action == WriteAction::Create
                                 ? L"Se creará la página \"" + w(pageName) + L"\":\n\n"
                                 : L"Se actualizará la página \"" + w(pageName) + L"\":\n\n");
        for (auto& line : plan.diff) text += L"  " + w(line) + L"\n";
        text += L"\nNo se toca ninguna otra página tuya. Podrás deshacerlo con un clic.";
        if (MessageBoxW(g->hwnd, text.c_str(), L"Aplicar runas", MB_OKCANCEL | MB_ICONQUESTION) !=
            IDOK)
            return;
    }
    WriteResult r = applyRunePage(*g->db, lcu, g->dd, g->lastRunePage, pageName, force);
    setText(IDC_PROFILE_STATUS, r.message);
    MessageBoxW(g->hwnd, w(r.message).c_str(), L"RiftLoop",
                MB_OK | (r.ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void undoRunesInClient() {
    if (!canUndoRunePage(*g->db)) {
        MessageBoxW(g->hwnd, L"No hay ninguna escritura de runas que deshacer.", L"RiftLoop",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    Lcu lcu;
    if (!lcu.connect(Config::load().leagueLockfilePath)) {
        MessageBoxW(g->hwnd, L"El cliente de League no está abierto.", L"RiftLoop",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    WriteResult r = undoRunePage(*g->db, lcu);
    setText(IDC_PROFILE_STATUS, r.message);
    MessageBoxW(g->hwnd, w(r.message).c_str(), L"RiftLoop",
                MB_OK | (r.ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

// Frees the thumbnails of the previous playlist. The report view draws them
// but never owns them.
void playClipRow(int row, bool autoplay = true);

void releaseClipThumbs() {
    for (HBITMAP b : g->clipThumbBitmaps) DeleteObject(b);
    g->clipThumbBitmaps.clear();
}

// Places the "generate" button: centred in the panel while the playlist is
// empty, tucked under the player once there are cards to play.
void layoutClipPanel(bool hasClips) {
    HWND btn = ctl(IDC_MAKE_CLIPS);
    HWND list = ctl(IDC_CLIP_LIST);
    if (!btn || !list) return;
    RECT lr;
    GetWindowRect(list, &lr);
    POINT tl{lr.left, lr.top};
    ScreenToClient(g->hwnd, &tl);
    int panelW = lr.right - lr.left;
    int panelH = lr.bottom - lr.top;
    if (hasClips) {
        // Out of the way: the list owns the panel.
        SetWindowPos(btn, nullptr, tl.x + (panelW - 260) / 2, tl.y + panelH + 10, 260, 32,
                     SWP_NOZORDER);
    } else {
        // Nothing to play: the one action available sits in the middle.
        SetWindowPos(btn, nullptr, tl.x + (panelW - 260) / 2, tl.y + panelH / 2 - 16, 260, 34,
                     SWP_NOZORDER);
    }
}

// Fills the playlist with the clips of the shown analysis: one card per
// evidence that has a clip on disk, with a still from the moment itself.
void refreshClipList() {
    HWND lv = ctl(IDC_CLIP_LIST);
    if (!lv) return;
    releaseClipThumbs();
    g->clipRowFiles.clear();
    std::vector<RVItem> items;

    if (!g->currentMatchId.empty()) {
        auto dir = util::clipDirFor(g->currentMatchId);
        if (auto a = g->db->loadAnalysis(g->currentMatchId)) {
            std::set<std::string> seen;      // one card per clip, never a repeat
            for (auto& f : a->findings) {
                for (auto& ev : f.evidence) {
                    if (ev.clipFile.empty() || !seen.insert(ev.clipFile).second) continue;
                    auto path = dir / ev.clipFile;
                    if (!std::filesystem::exists(path)) continue;
                    RVItem card;
                    card.kind = RVKind::ClipCard;
                    // The detector id is jargon and steals room from the title.
                    card.text = L"[" + w(util::formatGameClock(ev.gameTimestampMs)) + L"]  " +
                                w(f.title);
                    card.subtitle = w(ev.observedFacts);
                    // The moment itself sits past the lead margin.
                    card.thumb = grabFrame(path.wstring(), (double)kClipLeadMs / 1000.0, 96, 54);
                    if (card.thumb) g->clipThumbBitmaps.push_back(card.thumb);
                    card.action = "clip:" + ev.clipFile;
                    card.selected = items.empty();
                    items.push_back(std::move(card));
                    g->clipRowFiles.push_back(ev.clipFile);
                }
            }
        }
    }

    if (items.empty()) {
        items.push_back({RVKind::Dim,
                         L"Todavía no hay clips de esta partida.\n\nSi grabaste la partida, "
                         L"RiftLoop puede cortar los momentos que sostienen el diagnóstico."});
    }
    rvSet(lv, std::move(items));
    bool hasClips = !g->clipRowFiles.empty();
    layoutClipPanel(hasClips);
    // Nothing to play: hide the video surface and its bar instead of leaving a
    // dead frame and a set of controls that do nothing.
    if (g->playerWnd) {
        ShowWindow(g->playerWnd, hasClips ? SW_SHOW : SW_HIDE);
        showPlayerBar(g->playerWnd, hasClips && g->currentPage == 2);
    }
    if (hasClips) playClipRow(0, false);
    else setText(IDC_CLIP_STATUS, "");
}

void playClipFile(const std::string& fileName) {
    int row = -1;
    for (size_t i = 0; i < g->clipRowFiles.size(); ++i)
        if (g->clipRowFiles[i] == fileName) row = (int)i;
    playClipRow(row);
}

void playClipRow(int row, bool autoplay) {
    if (row < 0 || row >= (int)g->clipRowFiles.size()) return;
    HWND host = g->playerWnd;
    if (!host) return;
    if (!g->player) {
        g->player = std::make_unique<VideoPlayer>();
        if (!g->player->attach(host)) {
            g->player.reset();
            setText(IDC_CLIP_STATUS, "No se pudo iniciar el reproductor.");
            return;
        }
        bindPlayerHost(host, g->player.get());
    }
    auto path = util::clipDirFor(g->currentMatchId) / g->clipRowFiles[row];
    if (!g->player->load(path.wstring())) {
        setText(IDC_CLIP_STATUS, "No se pudo abrir el clip.");
        return;
    }
    RECT rc;
    GetClientRect(host, &rc);
    g->player->resize(rc.right, rc.bottom);
    if (autoplay) {
        g->player->play();
        setText(IDC_CLIP_STATUS, "Reproduciendo el momento.");
    } else {
        // Loaded and paused: the panel shows the first frame instead of black.
        g->player->play();
        g->player->pause();
        setText(IDC_CLIP_STATUS, "Listo para reproducir.");
    }
}

// Produces the evidence clips of the shown match. Cutting the recording takes
// a couple of seconds and needs nothing open; falling back to the client replay
// takes minutes and puts the game on screen, so it is offered, never assumed.
void makeClipsForCurrentMatch() {
    if (g->currentMatchId.empty()) return;
    std::string matchId = g->currentMatchId;

    if (hasRecordingFor(*g->db, matchId)) {
        SetWindowTextW(ctl(IDC_MAKE_CLIPS), L"Generando...");
        EnableWindow(ctl(IDC_MAKE_CLIPS), FALSE);
        setText(IDC_CLIP_STATUS, "Buscando los momentos destacados...");
        SendMessageW(ctl(IDC_CLIP_PROGRESS), PBM_SETRANGE32, 0, 100);
        SendMessageW(ctl(IDC_CLIP_PROGRESS), PBM_SETPOS, 0, 0);
        ShowWindow(ctl(IDC_CLIP_PROGRESS), SW_SHOW);
        std::thread([matchId] {
            Db db;
            auto res = makeClipsFromRecording(db, matchId, [](ClipProgress p) {
                {
                    std::lock_guard lk(g->clipMutex);
                    g->clipStatus = p.label;
                    g->clipStep = p.step;
                    g->clipTotal = p.total;
                }
                PostMessageW(g->hwnd, WM_APP_CLIPS, 1, 0);
            });
            {
                std::lock_guard lk(g->clipMutex);
                g->clipStatus = res.message;
            }
            PostMessageW(g->hwnd, WM_APP_CLIPS, 2, (LPARAM)res.made);
        }).detach();
        return;
    }

    // Clips already made and the recording already reduced: say so instead of
    // offering to redo the work from the client replay.
    if (!g->clipRowFiles.empty()) {
        MessageBoxW(g->hwnd,
                    L"Esta partida ya tiene sus clips y la grabación completa se borró al "
                    L"generarlos.\n\nSelecciona un clip en la lista para verlo.",
                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // No recording: the only other source is the client replay.
    if (!Config::load().clipsEnabled) {
        MessageBoxW(g->hwnd,
                    L"No hay grabación de esa partida.\n\n"
                    L"RiftLoop puede abrir el replay en el cliente y grabar los momentos, "
                    L"pero eso está desactivado. Actívalo en Ajustes.",
                    L"RiftLoop", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(g->hwnd,
                    L"No hay grabación de esa partida.\n\n"
                    L"Se puede abrir el replay en el cliente y grabar los momentos, pero "
                    L"tarda unos minutos y ocupa la pantalla. ¿Continuar?",
                    L"Generar clips", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    std::wstring exe = exeDirW() + L"\\RiftLoop.Analyzer.exe";
    std::wstring args = L"--clips " + w(matchId);
    ShellExecuteW(g->hwnd, L"open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
    setText(IDC_CLIP_STATUS, "Abriendo el replay en el cliente...");
}

void openRecordingsFolder() {
    auto dir = util::clipsDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ShellExecuteW(g->hwnd, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// "Liberar espacio". The two kinds of file cost different things: a full
// recording is a gigabyte of raw footage, and a clip is the proof behind a
// diagnosis. So the user picks which of the two goes, with both sizes on
// screen. Nothing is deleted without that pick: this is the user's own video
// (PRD 16.3).
void pruneRecordings() {
    RecordingsInfo info = recordingsInfo();
    if (info.files == 0 && info.clipFiles == 0) {
        MessageBoxW(g->hwnd, L"No hay grabaciones ni clips que borrar.", L"Liberar espacio",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    const int kOnlyRecordings = 101, kEverything = 102;
    std::wstring bRec = L"Borrar solo las grabaciones completas\nLibera " + humanSize(info.bytes) +
                        L". Los " + std::to_wstring(info.clipFiles) +
                        L" clips de evidencia se conservan.";
    std::wstring bAll = L"Borrar todo\nLibera " + humanSize(info.bytes + info.clipBytes) +
                        L". Los diagnósticos se quedan sin su prueba en vídeo.";
    TASKDIALOG_BUTTON buttons[2] = {{kOnlyRecordings, bRec.c_str()}, {kEverything, bAll.c_str()}};

    std::wstring content = L"Grabaciones completas: " + std::to_wstring(info.files) + L" · " +
                           humanSize(info.bytes) + L"\nClips de evidencia: " +
                           std::to_wstring(info.clipFiles) + L" · " + humanSize(info.clipBytes) +
                           L"\n\nEsto no se puede deshacer.";
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof cfg;
    cfg.hwndParent = g->hwnd;
    cfg.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    cfg.pszWindowTitle = L"Liberar espacio";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = L"¿Qué quieres borrar?";
    cfg.pszContent = content.c_str();
    cfg.pButtons = buttons;
    cfg.cButtons = 2;
    cfg.nDefaultButton = kOnlyRecordings;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr))) return;
    if (pressed != kOnlyRecordings && pressed != kEverything) return;
    const bool alsoClips = pressed == kEverything;

    // Collect first, delete after. Removing a file under the feet of a
    // directory iterator is not defined.
    std::error_code ec;
    auto dir = util::clipsDir();
    std::vector<std::filesystem::path> doomed;
    int recFiles = 0, clipFiles = 0;
    int64_t freed = 0;
    for (auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const bool top = e.path().parent_path() == dir;
        // At the top level the name test also catches the .mp4.json sidecar,
        // which is what we want: a recording leaves with its alignment data.
        if (top && isFullRecording(e.path().filename().string())) {
            if (e.path().extension() == ".mp4") ++recFiles;
        } else if (alsoClips && !top) {
            ++clipFiles;
        } else {
            continue;
        }
        freed += (int64_t)e.file_size(ec);
        doomed.push_back(e.path());
    }
    if (doomed.empty()) return;

    int removed = 0;
    for (auto& p2 : doomed)
        if (std::filesystem::remove(p2, ec)) ++removed;
    if (alsoClips)
        for (auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.is_directory(ec)) std::filesystem::remove(e.path(), ec);   // only if empty

    g->db->audit("recordings_cleaned", json{{"recordings", recFiles},
                                            {"clips", clipFiles},
                                            {"files_removed", removed},
                                            {"files_planned", (int)doomed.size()},
                                            {"freed_bytes", freed}}
                                           .dump());
    std::string status = std::to_string(recFiles) + " grabaciones y " +
                         std::to_string(clipFiles) + " clips borrados.";
    if (removed < (int)doomed.size())
        status += " " + std::to_string((int)doomed.size() - removed) +
                  " archivo(s) siguen en disco: estaban en uso.";
    setText(IDC_PROFILE_STATUS, status);
    refreshSettings();
    if (g->currentPage == 2) refreshClipList();
}

void openClip(const std::string& fileName) {
    auto path = util::clipDirFor(g->currentMatchId) / fileName;
    if (!std::filesystem::exists(path)) {
        MessageBoxW(g->hwnd, L"El clip ya no está en disco.", L"RiftLoop",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    ShellExecuteW(g->hwnd, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
                    if (s != "ChampSelect") clearDraftView();
                }
            } else if (type == "top3") {
                if (j.contains("draft")) applyDraftView(j.at("draft"));
                Top3 top = j.at("data").at("options").get<Top3>();
                rvSet(ctl(IDC_DRAFT_VIEW), rvTop3(top));
                if (g->currentPage != 4) switchPage(4);      // live champ select focus
            } else if (type == "plan") {
                if (j.contains("draft")) applyDraftView(j.at("draft"));
                const json& d = j.at("data").at("options");
                RunePlan rp = d.at("runes").get<RunePlan>();
                SpellPlan sp = d.at("spells").get<SpellPlan>();
                ItemPlan ip = d.at("items").get<ItemPlan>();
                g->quiz = j.value("quiz", std::vector<QuizQuestion>{});
                g->lastRunePage = rp.main;
                g->lastPlanChampion = d.value("champion", "");
                g->lastPlanRole = d.value("role", "");
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
    // The rank goes on the identity row, right of the save button. The rows
    // below it are already taken, and it belongs next to the Riot ID.
    mk(0, L"STATIC", L"", WS_VISIBLE, X + 880, Y + 24, 390, 22, IDC_RANK_LABEL);
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
                 W, 400, IDC_MATCH_LIST);
    addListViewColumns(ml, {{L"Campeón", 170}, {L"Rol", 90}, {L"Resultado", 90},
                            {L"KDA", 110}, {L"CS", 70}, {L"Oro", 80},
                            {L"Duración", 85}, {L"Parche", 70}, {L"Estado", 100}});
    g->matchIcons = ImageList_Create(28, 28, ILC_COLOR32, 32, 64);
    ListView_SetImageList(ml, g->matchIcons, LVSIL_SMALL);
    mkButton(1, L"⟳ Buscar partidas nuevas", X, Y + 412, 200, 30, IDC_MATCH_FETCH);
    mkButton(1, L"Ver análisis", X + 210, Y + 412, 140, 30, IDC_MATCH_OPEN);
    mkButton(1, L"Refrescar", X + 360, Y + 412, 120, 30, IDC_MATCH_REFRESH);
    // Ranks of a game already played. Never champion select (PRD 17.2): the
    // core refuses any id that is not a stored match.
    mkButton(1, L"Rangos de esta partida", X + 500, Y + 412, 200, 30, IDC_RANKS_FETCH);
    mkButton(1, L"Sólo mi rival de línea", X + 710, Y + 412, 190, 30, IDC_RANKS_ONE);
    mkButton(1, L"Forzar relectura", X + 910, Y + 412, 160, 30, IDC_RANKS_FORCE);
    mkReport(1, X, Y + 454, W, 350, IDC_RANKS_VIEW);

    // ---- 2: Post-match -----------------------------------------------------
    // Two columns: the diagnosis on the left, the clips that back it up on the
    // right. They never overlap, so the player can sit next to the text.
    const int postW = W - kClipPanelW - 24;      // left column
    const int clipX = X + W - kClipPanelW;       // right column origin
    // Lay out against the real client area: kWinH counts the title bar and the
    // borders, so using it pushed the last control off the bottom edge.
    RECT clientRc{};
    GetClientRect(g->hwnd, &clientRc);
    const int clientH = clientRc.bottom > 200 ? clientRc.bottom : kWinH - 40;
    const int postH = clientH - Y - 108;
    mkReport(2, X, Y, postW, postH, IDC_POST_VIEW);
    mk(2, L"STATIC", L"¿El diagnóstico principal es correcto?", WS_VISIBLE, X, Y + postH + 14,
       260, 24, 0);
    mkButton(2, L"Correcto", X + 270, Y + postH + 10, 96, 28, IDC_FB_OK);
    mkButton(2, L"Parcial", X + 372, Y + postH + 10, 96, 28, IDC_FB_PARTIAL);
    mkButton(2, L"Incorrecto", X + 474, Y + postH + 10, 96, 28, IDC_FB_WRONG);
    mkButton(2, L"Falta contexto", X + 576, Y + postH + 10, 120, 28, IDC_FB_CTX);
    mkButton(2, L"Aceptar misión sugerida", X, Y + postH + 50, 210, 32, IDC_ACCEPT_MISSION);

    // Right column.
    mk(2, L"STATIC", L"Clips de la partida", WS_VISIBLE, clipX, Y, kClipPanelW, 22, 0);
    HWND pv = CreateWindowW(L"RiftLoopPlayer", L"", WS_CHILD | WS_VISIBLE, clipX, Y + 28,
                            kClipPanelW, kClipPanelW * 9 / 16, g->hwnd,
                            (HMENU)(INT_PTR)IDC_CLIP_PLAYER, nullptr, nullptr);
    g->pageControls[2].push_back(pv);
    g->playerWnd = pv;
    const int playerBottom = Y + 28 + kClipPanelW * 9 / 16;
    // The player draws its own control bar just under the video (play, stop,
    // seek, full screen), so the panel only carries status.
    const int barBottom = playerBottom + 44;
    mk(2, L"STATIC", L"", WS_VISIBLE, clipX, barBottom + 8, kClipPanelW, 20, IDC_CLIP_STATUS);
    mk(2, PROGRESS_CLASSW, L"", 0, clipX, barBottom + 30, kClipPanelW, 4, IDC_CLIP_PROGRESS);
    const int listTop = barBottom + 44;
    const int listH = clientH - listTop - 52;    // room for the button below
    mkReport(2, clipX, listTop, kClipPanelW, listH, IDC_CLIP_LIST);
    // Centred in the panel while there is nothing to play; tucked under the
    // list once the playlist has cards (see layoutClipPanel).
    mkButton(2, L"Generar clips de evidencia", clipX + 60, listTop + listH + 10, 260, 32,
             IDC_MAKE_CLIPS);

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
    // No dropdowns here. The champion select fills this page; the user has
    // nothing to type. The strip of portraits does the reading.
    mkButton(4, L"Top 3", X, Y, 90, 28, IDC_DRAFT_GO);
    mkButton(4, L"Plan pregame", X + 98, Y, 120, 28, IDC_DRAFT_PLAN);
    mkButton(4, L"Probar quiz", X + 226, Y, 110, 28, IDC_DRAFT_QUIZ);
    mkButton(4, L"Aplicar runas", X + 616, Y, 150, 28, IDC_APPLY_RUNES);
    mkButton(4, L"Deshacer", X + 776, Y, 110, 28, IDC_UNDO_RUNES);
    mkReport(4, X, Y + 40, W, kDraftViewH, IDC_DRAFT_VIEW);
    rvSet(ctl(IDC_DRAFT_VIEW), rvDraftIdle());

    // ---- 5: Mapa -----------------------------------------------------------
    // The heat view owns its own legend and its own toggles, so the page only
    // adds how many matches to read.
    mkButton(5, L"20 partidas", X, Y, 130, 28, IDC_HEAT_20);
    mkButton(5, L"50 partidas", X + 140, Y, 130, 28, IDC_HEAT_50);
    mkButton(5, L"Solo mi rol", X + 280, Y, 130, 28, IDC_HEAT_ROLE);
    {
        HWND hv = CreateWindowW(L"RiftLoopHeatmap", L"", WS_CHILD | WS_VISIBLE, X, Y + 40, W,
                                kWinH - Y - 96, g->hwnd, (HMENU)(INT_PTR)IDC_HEAT_VIEW, nullptr,
                                nullptr);
        g->pageControls[5].push_back(hv);
    }

    // ---- 6: Curvas ---------------------------------------------------------
    mkButton(6, L"Anterior", X, Y, 110, 28, IDC_CURVE_PREV);
    mkButton(6, L"Siguiente", X + 120, Y, 110, 28, IDC_CURVE_NEXT);
    {
        HWND cv = CreateWindowW(L"RiftLoopCurves", L"", WS_CHILD | WS_VISIBLE, X, Y + 40, W,
                                kWinH - Y - 96, g->hwnd, (HMENU)(INT_PTR)IDC_CURVE_VIEW, nullptr,
                                nullptr);
        g->pageControls[6].push_back(cv);
    }

    // ---- 7: Parche ---------------------------------------------------------
    mkReport(7, X, Y, W, 480, IDC_PATCH_VIEW);
    mkButton(7, L"Actualizar", X, Y + 492, 130, 30, IDC_PATCH_REFRESH);

    // ---- 8: Ajustes --------------------------------------------------------
    mk(8, L"BUTTON", L"Overlay en partida", WS_VISIBLE | BS_AUTOCHECKBOX, X, Y, 220, 26,
       IDC_SET_OVERLAY);
    mk(8, L"BUTTON", L"Lectura del cliente (LCU)", WS_VISIBLE | BS_AUTOCHECKBOX, X + 240, Y, 240,
       26, IDC_SET_LCU);
    mk(8, L"BUTTON", L"Grabación local (beta)", WS_VISIBLE | BS_AUTOCHECKBOX, X + 500, Y, 240, 26,
       IDC_SET_CAPTURE);
    mk(8, L"BUTTON", L"Clips de evidencia desde el replay", WS_VISIBLE | BS_AUTOCHECKBOX, X,
       Y + 108, 340, 26, IDC_SET_CLIPS);
    mk(8, L"BUTTON", L"Aplicar runas al cliente (siempre con un clic tuyo)",
       WS_VISIBLE | BS_AUTOCHECKBOX, X + 360, Y + 108, 420, 26, IDC_SET_RUNEWRITE);
    mk(8, L"BUTTON", L"Conservar la partida completa además de los clips",
       WS_VISIBLE | BS_AUTOCHECKBOX, X, Y + 178, 420, 26, IDC_SET_KEEPFULL);
    mk(8, L"STATIC", L"Ruta del lockfile (vacío = autodetectar)", WS_VISIBLE, X, Y + 40, 280, 22,
       0);
    mk(8, L"EDIT", L"", WS_VISIBLE | WS_BORDER, X + 290, Y + 38, 380, 26, IDC_SET_LOCKFILE);
    // The Riot key. It never lands in a file of this repository and never in
    // plain text: Config::saveApiKey seals it with DPAPI, so only this Windows
    // account can read it back (PRD 16, 21.2, 29).
    mk(8, L"STATIC", L"Clave de Riot (opcional, para el rango de los rivales)", WS_VISIBLE,
       X + 760, Y, 420, 22, 0);
    mk(8, L"EDIT", L"", WS_VISIBLE | WS_BORDER | ES_PASSWORD, X + 760, Y + 24, 300, 26,
       IDC_SET_APIKEY);
    mkButton(8, L"Guardar clave", X + 760, Y + 58, 140, 30, IDC_SET_APIKEY_SAVE);
    mkButton(8, L"Borrar clave", X + 910, Y + 58, 140, 30, IDC_SET_APIKEY_CLEAR);
    mk(8, L"STATIC", L"", WS_VISIBLE, X + 760, Y + 96, 420, 40, IDC_SET_APIKEY_STATE);

    mkButton(8, L"Abrir grabaciones", X, Y + 142, 170, 30, IDC_OPEN_CLIPS);
    mkButton(8, L"Liberar espacio", X + 180, Y + 142, 160, 30, IDC_PRUNE_CLIPS);
    mkButton(8, L"Guardar ajustes", X, Y + 78, 150, 30, IDC_SET_SAVE);
    mkButton(8, L"Log de auditoría", X + 160, Y + 78, 150, 30, IDC_SET_AUDIT);
    mkButton(8, L"Actualizar muestra", X + 320, Y + 78, 180, 30, IDC_SET_META);
    mkButton(8, L"Borrar TODOS los datos", X + 510, Y + 78, 200, 30, IDC_SET_WIPE);
    mkReport(8, X, Y + 210, W, 314, IDC_SET_VIEW);
}

// Draws the curves of one match. The page walks the stored matches with the
// two buttons, so a reader can compare a game against the next one.
void refreshCurves() {
    HWND view = ctl(IDC_CURVE_VIEW);
    if (!view) return;
    auto rows = g->db->listMatches(50);
    if (rows.empty()) {
        rl::MatchCurves empty;
        empty.note = "No hay partidas importadas todavia. Abre Partidas e importa desde el cliente.";
        cvSet(view, std::move(empty));
        return;
    }
    if (g->curveIndex < 0) g->curveIndex = 0;
    if (g->curveIndex >= (int)rows.size()) g->curveIndex = (int)rows.size() - 1;
    cvSet(view, rl::buildCurves(*g->db, rows[g->curveIndex].matchId));
}

// Reads the last N stored matches and hands the cloud to the control. It never
// downloads a match: everything here is already on disk (EP-03).
void refreshHeatmap() {
    HWND view = ctl(IDC_HEAT_VIEW);
    if (!view) return;
    rl::HeatQuery q;
    q.matches = g->heatMatches;
    if (g->heatRoleOnly) q.role = g->db->loadProfile().preferredRoles.empty()
                               ? std::string()
                               : g->db->loadProfile().preferredRoles.front();
    rl::HeatMap h = collectHeat(*g->db, q);

    rlui::HeatViewData d;
    d.points = std::move(h.points);
    d.matches = h.matchesRead;
    d.note = w(h.note);
    if (g->ddOk) d.mapUrl = g->dd.mapIconUrl(11);
    hmSet(view, std::move(d));
}

// Asks Riot for the ranks the cache lacks. The call is spaced so a development
// key stays inside its quota, and the user is told how long it takes before it
// starts. onlyLaneRival limits it to the enemy in the user's own position.
void fetchMatchRanks(bool onlyLaneRival, bool force) {
    std::string matchId = selectedMatchId();
    if (matchId.empty()) {
        setText(IDC_PROFILE_STATUS, "Elige una partida de la lista primero.");
        return;
    }
    RiotApiConfig api;
    api.apiKey = Config::loadApiKey();
    api.routing = Config::load().routing;

    int only = 0;
    auto match = parseMatch(g->db->matchJson(matchId));
    if (onlyLaneRival && match) {
        std::string puuid = resolveUserPuuid(*g->db);
        const Participant* me = puuid.empty() ? nullptr : match->byPuuid(puuid);
        if (me && !me->position.empty()) {
            for (auto& p : match->participants)
                if (p.teamId != me->teamId && p.position == me->position)
                    only = p.participantId;
        }
        if (only == 0) {
            setText(IDC_PROFILE_STATUS,
                    "No hay un rival en tu misma posicion en esa partida.");
            return;
        }
    }

    int players = only ? 1 : (match ? (int)match->participants.size() : 10);
    if (!only) {
        std::wstring msg = L"Voy a leer el rango de " + std::to_wstring(players) +
                           L" jugadores. Son " + std::to_wstring(players * 2) +
                           L" llamadas espaciadas para no agotar tu cuota, unos " +
                           std::to_wstring(estimatedRefreshSeconds(players)) +
                           L" s. La ventana se queda quieta mientras tanto.\n\n¿Sigo?";
        if (MessageBoxW(g->hwnd, msg.c_str(), L"Rangos de la partida",
                        MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
            return;
    }

    auto res = refreshMatchRanks(*g->db, matchId, api, only, force);
    setText(IDC_PROFILE_STATUS, res.message);
    rvSet(ctl(IDC_RANKS_VIEW), rvMatchRanks(matchId));
}

// Stores the Riot key, or removes it. It is written through DPAPI and never
// echoed back to the box (TASK-0025).
void saveApiKeyFromUi(bool clear) {
    if (clear) {
        Config::saveApiKey("");
        g->db->audit("api_key", "{\"action\":\"cleared\"}");
        setText(IDC_PROFILE_STATUS, "Clave de Riot borrada.");
        refreshSettings();
        return;
    }
    std::string key = n(ctl(IDC_SET_APIKEY));
    // Trim: a key pasted from a browser often carries spaces or a newline.
    while (!key.empty() && (unsigned char)key.front() <= ' ') key.erase(key.begin());
    while (!key.empty() && (unsigned char)key.back() <= ' ') key.pop_back();
    if (key.empty()) {
        setText(IDC_PROFILE_STATUS, "Escribe la clave en el campo antes de guardarla.");
        return;
    }
    if (key.rfind("RGAPI-", 0) != 0) {
        setText(IDC_PROFILE_STATUS,
                "Una clave de Riot empieza por RGAPI-. Copiala entera desde "
                "developer.riotgames.com.");
        return;
    }
    Config::saveApiKey(key);
    g->db->audit("api_key", "{\"action\":\"saved\"}");
    setText(IDC_PROFILE_STATUS, "Clave guardada y protegida con DPAPI.");
    refreshSettings();
}

void switchPage(int page) {
    g->currentPage = page;
    for (int p = 0; p < kPages; ++p)
        for (HWND h : g->pageControls[p])
            ShowWindow(h, p == page ? SW_SHOW : SW_HIDE);
    // The control bar is a sibling of the video, not a page control, so it has
    // to be hidden by hand or it floats over every other page.
    if (g->playerWnd) showPlayerBar(g->playerWnd, page == 2 && !g->clipRowFiles.empty());
    switch (page) {
        case 0: refreshProfileUi(); break;
        case 1:
            refreshMatchList();
            rvSet(ctl(IDC_RANKS_VIEW), rvMatchRanks(selectedMatchId()));
            break;
        case 2:
            // Opening the page with nothing loaded showed an empty panel. Fall
            // back to the most recent analysed match.
            if (g->currentMatchId.empty()) {
                for (auto& r : g->db->listMatches(10))
                    if (r.analyzed) { showAnalysis(r.matchId); break; }
            } else {
                refreshClipList();
            }
            break;
        case 3: refreshMission(); break;
        case 5: refreshHeatmap(); break;
        case 6: refreshCurves(); break;
        case 7: rvSet(ctl(IDC_PATCH_VIEW), rvPatch()); break;
        case 8: refreshSettings(); break;
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
    // Version and build stamp: without them there is no way to tell whether the
    // running binary is the one just compiled.
    std::wstring ver = L"v" + w(kAppVersion) + L" · " + w(appBuildStamp());
    TextOutW(dc, 24, 70, ver.c_str(), (int)ver.size());

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
            if (hdr->idFrom == IDC_MATCH_LIST && hdr->code == LVN_ITEMCHANGED) {
                auto* nv = (NMLISTVIEW*)lp;
                // Only the transition into "selected". LVN_ITEMCHANGED also
                // fires for focus and for the row that just lost the mark.
                if ((nv->uNewState & LVIS_SELECTED) && !(nv->uOldState & LVIS_SELECTED))
                    rvSet(ctl(IDC_RANKS_VIEW), rvMatchRanks(selectedMatchId()));
                return 0;
            }
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
            if (id == IDC_HEAT_20 || id == IDC_HEAT_50) {
                g->heatMatches = id == IDC_HEAT_20 ? 20 : 50;
                refreshHeatmap();
                return 0;
            }
            if (id == IDC_SET_APIKEY_SAVE || id == IDC_SET_APIKEY_CLEAR) {
                saveApiKeyFromUi(id == IDC_SET_APIKEY_CLEAR);
                return 0;
            }
            if (id == IDC_RANKS_FETCH || id == IDC_RANKS_ONE || id == IDC_RANKS_FORCE) {
                fetchMatchRanks(id == IDC_RANKS_ONE, id == IDC_RANKS_FORCE);
                return 0;
            }
            if (id == IDC_CURVE_PREV || id == IDC_CURVE_NEXT) {
                g->curveIndex += id == IDC_CURVE_NEXT ? 1 : -1;
                refreshCurves();
                return 0;
            }
            if (id == IDC_HEAT_ROLE) {
                g->heatRoleOnly = !g->heatRoleOnly;
                refreshHeatmap();
                return 0;
            }
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
                case IDC_FETCH_API:
                case IDC_MATCH_FETCH: runFetch(); break;
                case IDC_ANALYZE: runAnalysis(); break;
                case IDC_MATCH_REFRESH:
                    // The button worked in silence, so it read as dead.
                    refreshMatchList();
                    rvSet(ctl(IDC_RANKS_VIEW), rvMatchRanks(selectedMatchId()));
                    setText(IDC_PROFILE_STATUS,
                            std::to_string(g->matchRowIds.size()) +
                            " partidas releidas de la base local.");
                    break;
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
                case IDC_MAKE_CLIPS: makeClipsForCurrentMatch(); break;
                case IDC_CLIP_FULL:
                    togglePlayerFullscreen(g->playerWnd);
                    break;
                case IDC_CLIP_PLAYPAUSE:
                    if (g->player) {
                        if (g->player->playing()) g->player->pause();
                        else g->player->play();
                    }
                    break;
                case IDC_APPLY_RUNES: applyRunesToClient(); break;
                case IDC_UNDO_RUNES: undoRunesInClient(); break;
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
                case IDC_SET_META: refreshMetaSample(); break;
                case IDC_OPEN_CLIPS: openRecordingsFolder(); break;
                case IDC_PRUNE_CLIPS: pruneRecordings(); break;
                case IDC_DRAFT_GO: draftRecommend(); break;
                case IDC_DRAFT_PLAN: draftPlan(); break;
                case IDC_DRAFT_QUIZ: openQuizWindow(); break;
                case IDC_PATCH_REFRESH: rvSet(ctl(IDC_PATCH_VIEW), rvPatch()); break;
                case IDC_SET_SAVE: {
                    Config cfg = Config::load();
                    cfg.overlayEnabled = IsDlgButtonChecked(hwnd, IDC_SET_OVERLAY) == BST_CHECKED;
                    cfg.lcuReadEnabled = IsDlgButtonChecked(hwnd, IDC_SET_LCU) == BST_CHECKED;
                    cfg.captureEnabled = IsDlgButtonChecked(hwnd, IDC_SET_CAPTURE) == BST_CHECKED;
                    cfg.clipsEnabled = IsDlgButtonChecked(hwnd, IDC_SET_CLIPS) == BST_CHECKED;
                    cfg.runeWriteEnabled =
                        IsDlgButtonChecked(hwnd, IDC_SET_RUNEWRITE) == BST_CHECKED;
                    cfg.keepFullRecording =
                        IsDlgButtonChecked(hwnd, IDC_SET_KEEPFULL) == BST_CHECKED;
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
        case WM_APP_CLIPS: {
            std::string status;
            int step = 0, total = 0;
            {
                std::lock_guard lk(g->clipMutex);
                status = g->clipStatus;
                step = g->clipStep;
                total = g->clipTotal;
            }
            setText(IDC_CLIP_STATUS, status);
            if (wp == 1 && total > 0)
                SendMessageW(ctl(IDC_CLIP_PROGRESS), PBM_SETPOS, (WPARAM)(step * 100 / total), 0);
            if (wp == 2) {
                SetWindowTextW(ctl(IDC_MAKE_CLIPS), L"Generar clips de evidencia");
                EnableWindow(ctl(IDC_MAKE_CLIPS), TRUE);
                if ((int)lp > 0) {
                    SendMessageW(ctl(IDC_CLIP_PROGRESS), PBM_SETPOS, 100, 0);
                    showAnalysis(g->currentMatchId);   // reload with the clip links
                    refreshClipList();
                    refreshSettings();
                } else {
                    // The run made no clip. The status label sits at the top of
                    // the panel, far from the button, so a refusal shown only
                    // there reads as "the button does nothing". Say it here too.
                    ShowWindow(ctl(IDC_CLIP_PROGRESS), SW_HIDE);
                    std::string why = status.empty()
                        ? "No se pudo generar ningun clip de esta partida."
                        : status;
                    MessageBoxW(g->hwnd, w(why).c_str(), L"Generar clips",
                                MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }
        case WM_RV_ACTION: {
            std::string action = rvActionAt((HWND)lp, (int)wp);
            if (action.rfind("clip:", 0) != 0) return 0;
            std::string file = action.substr(5);
            // A card in the playlist plays inline; the link inside the
            // diagnosis text does the same, so the video always shows up next
            // to the sentence it backs up.
            playClipFile(file);
            // Highlight the card being played, wherever the click came from.
            HWND list = ctl(IDC_CLIP_LIST);
            for (size_t i = 0; i < g->clipRowFiles.size(); ++i)
                if (g->clipRowFiles[i] == file) rvHighlight(list, (int)i);
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
            } else if (wp == 3) {
                EnableWindow(ctl(IDC_SET_META), TRUE);
                setText(IDC_SET_META, "Actualizar muestra");
                int rows = (int)lp;
                setText(IDC_PROFILE_STATUS, rows < 0
                    ? "No se pudo actualizar la muestra local."
                    : "Muestra local: " + std::to_string(rows) + " builds.");
                refreshSettings();
            } else if (wp == 2) {
                EnableWindow(ctl(IDC_FETCH_API), TRUE);
                EnableWindow(ctl(IDC_MATCH_FETCH), TRUE);
                int imported = (int)LOWORD(lp) - 1;
                int analyzed = (int)HIWORD(lp);
                setText(IDC_PROFILE_STATUS, imported < 0
                    ? "No se pudo leer el historial: abre el cliente de League y reintenta."
                    : std::to_string(imported) + " partidas nuevas, " +
                      std::to_string(analyzed) + " analizadas.");
                refreshMatchList();
                refreshMission();
                if (g->currentPage == 0) refreshProfileUi();
                if (analyzed > 0) {
                    auto rows = g->db->listMatches(1);
                    if (!rows.empty() && rows[0].analyzed) showAnalysis(rows[0].matchId);
                }
            } else {
                processIpcQueue();
            }
            return 0;
        case WM_APP_ICONS:
            // A batch of Data Dragon icons landed: repaint what shows them.
            for (int id : {IDC_POST_VIEW, IDC_DRAFT_VIEW, IDC_PATCH_VIEW, IDC_MISSION_VIEW,
                           IDC_HEAT_VIEW,
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
    registerPlayerView(hInst);
    registerHeatView(hInst);
    registerCurveView(hInst);
    registerTipWindow(hInst);
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
    }
    std::wstring cmdLine = GetCommandLineW();
    if (cmdLine.find(L"--demo-draft") != std::wstring::npos) {
        loadDemoDraft();
        rvSet(ctl(IDC_DRAFT_VIEW), rvTop3(recommendTop3(*app.db, app.dd, draftContextFromUi())));
        switchPage(4);
    } else if (size_t at = cmdLine.find(L"--page "); at != std::wstring::npos) {
        // Opens straight on one page. A development flag: a screenshot of a
        // page needs no click, and clicking the window steals the focus of a
        // user who is playing.
        int page = _wtoi(cmdLine.c_str() + at + 7);
        switchPage(page >= 0 && page < kPages ? page : 0);
    } else {
        switchPage(0);
    }
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
