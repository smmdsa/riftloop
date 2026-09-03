// RiftLoop.Agent: persistent lightweight Win32 process (PRD 14.2).
// State machine over the LCU (read-only), IPC broadcaster, tray icon and
// orchestration of Overlay/Analyzer. It never writes to the League client.
#include "core/analysis.h"
#include "core/config.h"
#include "core/contracts.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/ipc.h"
#include "core/lcu.h"
#include "core/lcu_history.h"
#include "core/imagecache.h"
#include "core/planner.h"
#include "core/recommend.h"
#include "core/serial.h"
#include "core/util.h"

#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <thread>
#include <vector>
#include <optional>
#include <string>

using namespace rl;
using nlohmann::json;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT kTimerPoll = 1;
constexpr int kPollMs = 1000;        // >= 250 ms rule (PRD 15.4)

constexpr int kMenuOpenDesktop = 100;
constexpr int kMenuToggleOverlay = 101;
constexpr int kMenuExit = 102;

struct AgentState {
    std::unique_ptr<Db> db;
    Ddragon dd;
    bool ddOk = false;
    Config cfg;
    Lcu lcu;
    ipc::Server server;
    GameState state = GameState::NoClient;
    int64_t lastLcuAttempt = 0;
    int64_t lastDataCheck = 0;
    std::string dataLocale;
    std::string lastDraftSignature;  // avoid recompute without draft change (RF-CS-005)
    bool overlaySpawned = false;
    bool analyzerRunning = false;
    HANDLE analyzerProcess = nullptr;
    NOTIFYICONDATAW nid{};
};

AgentState* g = nullptr;

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s = buf;
    return s.substr(0, s.find_last_of(L'\\'));
}

void spawn(const std::wstring& exe, const std::wstring& args, HANDLE* proc = nullptr) {
    std::wstring cmd = L"\"" + exeDir() + L"\\" + exe + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        if (proc) *proc = pi.hProcess;
        else CloseHandle(pi.hProcess);
    }
}

void broadcastState() {
    json j;
    j["v"] = ipc::kProtocolVersion;
    j["type"] = "state";
    j["state"] = toString(g->state);
    g->server.broadcast(j.dump());
}

// Champion-id list -> ddragon name list.
std::vector<std::string> namesOf(const std::vector<int>& ids) {
    std::vector<std::string> out;
    for (int id : ids)
        if (const ChampInfo* c = g->dd.championByKey(id)) out.push_back(c->id);
    return out;
}

void handleChampSelect() {
    auto cs = g->lcu.champSelect();
    if (!cs) return;

    DraftContext ctx;
    ctx.role = cs->assignedRole;
    ctx.allyChampions = namesOf(cs->allyChampionIds);
    ctx.enemyChampions = namesOf(cs->enemyChampionIds);
    ctx.bans = namesOf(cs->banIds);
    ctx.ownedOrPickable = namesOf(cs->pickableChampionIds);
    ctx.patch = g->dd.version();

    // Recompute only when the draft changed (RF-CS-005).
    std::string sig = ctx.role + "|" + std::to_string(cs->localChampionId);
    for (auto& c : ctx.allyChampions) sig += "," + c;
    sig += "|";
    for (auto& c : ctx.enemyChampions) sig += "," + c;
    if (sig == g->lastDraftSignature) return;
    g->lastDraftSignature = sig;

    if (cs->localChampionId == 0) {
        // Not locked yet: Top 3.
        Top3 top = recommendTop3(*g->db, g->dd, ctx);
        json contract = makeContract("top3", ctx.patch,
                                     json{{"role", ctx.role},
                                          {"known_picks", ctx.allyChampions.size() +
                                                          ctx.enemyChampions.size()}},
                                     json(top), top.cards.empty() ? "baja" : top.cards[0].confidence,
                                     top.uncertaintyReason);
        g->db->saveRecommendation("top3", contract.dump());
        json msg;
        msg["v"] = 1;
        msg["type"] = "top3";
        msg["data"] = contract;
        g->server.broadcast(msg.dump());
    } else if (const ChampInfo* mine = g->dd.championByKey(cs->localChampionId)) {
        // Locked: full pregame plan + quiz.
        PlanInput pi;
        pi.champion = mine->id;
        pi.role = ctx.role;
        pi.draft = ctx;
        RunePlan runes = planRunes(g->dd, pi);
        SpellPlan spells = planSpells(g->dd, pi);
        ItemPlan items = planItems(*g->db, g->dd, pi);
        auto quiz = buildQuiz(g->dd, pi);

        json payload;
        payload["champion"] = pi.champion;
        payload["role"] = pi.role;
        payload["runes"] = runes;
        payload["spells"] = spells;
        payload["items"] = items;
        json contract = makeContract("pregame_plan", ctx.patch,
                                     json{{"champion", pi.champion}, {"role", pi.role},
                                          {"enemies", ctx.enemyChampions}},
                                     payload, items.confidence, "matchup de linea sin confirmar");
        g->db->saveRecommendation("pregame_plan", contract.dump());
        g->db->saveRecommendation("quiz", json(quiz).dump());

        json msg;
        msg["v"] = 1;
        msg["type"] = "plan";
        msg["data"] = contract;
        msg["quiz"] = quiz;
        g->server.broadcast(msg.dump());

        // Prefetch icons so the overlay draws from local files only.
        std::vector<std::pair<std::string, std::string>> downloads;   // id, url
        auto addItem = [&](int id) {
            downloads.push_back({std::to_string(id), g->dd.itemIconUrl(id)});
        };
        for (int id : items.starting) addItem(id);
        for (int id : items.core) addItem(id);
        for (auto& b : items.boots) for (int id : b.items) addItem(id);
        for (auto& b : items.branches) for (int id : b.items) addItem(id);
        std::thread([downloads] {
            for (auto& [id, url] : downloads) img::ensure("item", id, url);
        }).detach();
    }
}

void onEnterState(GameState prev, GameState next) {
    g->db->audit("state_change", json{{"from", toString(prev)}, {"to", toString(next)}}.dump());

    if (next == GameState::ChampSelect) g->lastDraftSignature.clear();

    if (next == GameState::Loading) {
        json msg;
        msg["v"] = 1;
        msg["type"] = "quiz_show";
        g->server.broadcast(msg.dump());
    }
    if (next == GameState::InGame) {
        // Quiz closes when the game starts (RF-QUIZ-001).
        g->server.broadcast(json{{"v", 1}, {"type", "quiz_hide"}}.dump());
        if (g->cfg.overlayEnabled && !g->overlaySpawned) {
            spawn(L"RiftLoop.Overlay.exe", L"");
            g->overlaySpawned = true;
        }
        if (g->cfg.captureEnabled) {
            // Opt-in recording; the process dies with the game window (RF-REC).
            spawn(L"RiftLoop.Capture.exe", L"");
            g->db->audit("capture_started", "{\"source\":\"game_window\"}");
        }
    }
    if (next == GameState::PostGame && prev == GameState::InGame) {
        // Fetch (when a key exists) and analyze at low priority (PRD 14.2).
        if (!g->analyzerRunning) {
            g->analyzerRunning = true;
            spawn(L"RiftLoop.Analyzer.exe", L"--auto", &g->analyzerProcess);
        }
    }
}

void poll() {
    // Reconnect to the LCU at most every 5 s when absent (no tight loop).
    if (!g->lcu.connected()) {
        int64_t now = GetTickCount64();
        if (now - g->lastLcuAttempt > 5000) {
            g->lastLcuAttempt = now;
            g->lcu.connect(g->cfg.leagueLockfilePath);
        }
    }
    GameState next = g->state;
    if (g->cfg.lcuReadEnabled && g->lcu.connected()) {
        std::string phase = g->lcu.gameflowPhase();
        if (phase.empty()) {
            g->lcu = Lcu{};          // stale lockfile: back to detection
            next = GameState::NoClient;
        } else {
            next = g->lcu.toGameState(phase);
        }
    } else if (!g->lcu.connected()) {
        next = GameState::NoClient;
    }

    if (next != g->state) {
        GameState prev = g->state;
        g->state = next;
        onEnterState(prev, next);
    }
    // Heartbeat: clients that connect mid-phase still learn the state.
    broadcastState();
    if (g->state == GameState::ChampSelect) handleChampSelect();

    // Refresh static data while idle: new patch or client locale change.
    bool idle = g->state == GameState::NoClient || g->state == GameState::ClientOpen ||
                g->state == GameState::Lobby;
    int64_t now2 = GetTickCount64();
    std::string wanted = resolveDataLocale(*g->db, g->lcu.connected() ? &g->lcu : nullptr,
                                           g->cfg.dataLocale);
    bool localeChanged = g->ddOk && wanted != g->dataLocale;
    if (idle && (localeChanged || now2 - g->lastDataCheck > 6LL * 3600 * 1000)) {
        g->lastDataCheck = now2;
        g->dataLocale = wanted;
        bool ok = g->dd.load(true, wanted);
        if (ok) g->ddOk = true;
        g->db->setKv("last_data_version", g->dd.version());
    }

    // Analyzer finished? Tell the Desktop.
    if (g->analyzerRunning && g->analyzerProcess &&
        WaitForSingleObject(g->analyzerProcess, 0) == WAIT_OBJECT_0) {
        CloseHandle(g->analyzerProcess);
        g->analyzerProcess = nullptr;
        g->analyzerRunning = false;
        g->server.broadcast(json{{"v", 1}, {"type", "analysis_ready"}}.dump());
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kTimerPoll) poll();
            return 0;
        case WM_TRAY:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, kMenuOpenDesktop, L"Abrir panel");
                AppendMenuW(menu, MF_STRING | (g->cfg.overlayEnabled ? MF_CHECKED : 0),
                            kMenuToggleOverlay, L"Overlay en partida");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kMenuExit, L"Salir");
                SetForegroundWindow(hwnd);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kMenuOpenDesktop: spawn(L"RiftLoop.Desktop.exe", L""); break;
                case kMenuToggleOverlay:
                    g->cfg.overlayEnabled = !g->cfg.overlayEnabled;
                    g->cfg.save();
                    break;
                case kMenuExit: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g->nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // Single instance.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"RiftLoop.Agent.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    AgentState state;
    g = &state;
    try {
        state.db = std::make_unique<Db>();
    } catch (...) {
        MessageBoxW(nullptr, L"No se pudo abrir la base de datos local.", L"RiftLoop", MB_ICONERROR);
        return 1;
    }
    state.cfg = Config::load();
    state.lcu.connect(state.cfg.leagueLockfilePath);
    state.dataLocale = resolveDataLocale(*state.db, &state.lcu, state.cfg.dataLocale);
    state.ddOk = state.dd.load(true, state.dataLocale);
    state.lastDataCheck = GetTickCount64();
    state.server.start();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RiftLoopAgentWnd";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"RiftLoop Agent", 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, hInst, nullptr);

    state.nid.cbSize = sizeof state.nid;
    state.nid.hWnd = hwnd;
    state.nid.uID = 1;
    state.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    state.nid.uCallbackMessage = WM_TRAY;
    state.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(state.nid.szTip, L"RiftLoop (read-only)");
    Shell_NotifyIconW(NIM_ADD, &state.nid);

    SetTimer(hwnd, kTimerPoll, kPollMs, nullptr);
    broadcastState();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    state.server.stop();
    if (mutex) CloseHandle(mutex);
    return 0;
}
