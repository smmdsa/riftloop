// Desktop UI infrastructure: dark theme, async Data Dragon icon store and the
// ReportView custom control (icon + text report rendering with scroll).
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace rlui {

// ------------------------------------------------------------------- theme
namespace theme {
inline constexpr COLORREF kBg      = RGB(15, 19, 28);    // window background
inline constexpr COLORREF kSidebar = RGB(10, 13, 20);
inline constexpr COLORREF kCard    = RGB(24, 30, 43);    // panels, edits
inline constexpr COLORREF kCardHi  = RGB(33, 41, 58);
inline constexpr COLORREF kBorder  = RGB(45, 55, 75);
inline constexpr COLORREF kText    = RGB(230, 233, 239);
inline constexpr COLORREF kDim     = RGB(138, 147, 165);
inline constexpr COLORREF kAccent  = RGB(76, 194, 255);
inline constexpr COLORREF kGood    = RGB(126, 231, 135);
inline constexpr COLORREF kWarn    = RGB(255, 196, 87);
inline constexpr COLORREF kDanger  = RGB(255, 123, 114);

HBRUSH bgBrush();
HBRUSH sidebarBrush();
HBRUSH cardBrush();

HFONT h1();      // 24 semibold
HFONT h2();      // 17 semibold
HFONT body();    // 15
HFONT small_();  // 13
HFONT tiny();    // 11.5
} // namespace theme

// Applies dark mode to the title bar and dark explorer theme to a control.
void applyDarkTitleBar(HWND hwnd);
void applyDarkTheme(HWND control);

// -------------------------------------------------------------- icon store
// Async downloader over rl::img cache. request() queues a miss; when a batch
// lands, the notify window receives WM_APP_ICONS (wparam 0).
inline constexpr UINT WM_APP_ICONS = WM_APP + 40;

namespace icons {
void init(HWND notifyWindow);
void shutdown();
// kind: "champ" | "item" | "perk" | "spell". Returns a 32bpp HBITMAP with
// alpha, or nullptr while the icon is not cached yet (a fetch is queued).
HBITMAP get(const std::string& kind, const std::string& id, const std::string& url);
// Only look in memory/disk; never queues a download.
HBITMAP peek(const std::string& kind, const std::string& id);
} // namespace icons

// Draws a 32bpp bitmap with alpha at the given size.
void drawBitmap(HDC dc, HBITMAP bmp, int x, int y, int size);

// ------------------------------------------------------------- report view
// Item kinds for the ReportView control.
enum class RVKind { Title, Section, Text, Dim, IconRow, Badge, Spacer, ClipCard, TeamStrip };

// One champion in a draft strip. An empty seat keeps its place, so a pick that
// lands late never moves the ones already drawn.
struct RVSeat {
    std::string  champion;           // ddragon id; empty = the seat is open
    std::string  iconUrl;
    bool         banned = false;     // drawn dimmed and struck through
    bool         hover = false;      // picked but not locked: it can still change
    bool         isUser = false;     // the user's own champion
    std::wstring label;              // role or position, under the portrait
};

struct RVItem {
    RVKind kind = RVKind::Text;
    std::wstring text;
    std::wstring right;              // right-aligned secondary text (badges)
    std::string iconKind, iconId, iconUrl;   // IconRow / Title portrait
    COLORREF color = 0;              // 0 = default for the kind
    int indent = 0;                  // extra left indent in px
    // Non-empty makes the row clickable: the view posts WM_RV_ACTION to its
    // parent and the parent reads the string back with rvActionAt().
    std::string action;
    // ClipCard only: thumbnail on the left and a second line under the title.
    // The view draws the bitmap but never owns it; the caller frees it.
    HBITMAP      thumb = nullptr;
    std::wstring subtitle;
    bool         selected = false;
    // TeamStrip only: two rows of portraits facing each other. left is drawn
    // left-aligned in leftColor, right is right-aligned in rightColor, and
    // `text` names the row between them.
    std::vector<RVSeat> leftSeats, rightSeats;
    COLORREF     leftColor = 0, rightColor = 0;
    bool         compact = false;    // smaller portraits: the bans row
};

// wParam = item index, lParam = the view HWND.
inline constexpr UINT WM_RV_ACTION = WM_APP + 60;

// Registers the "RiftLoopReport" window class. Call once before CreateWindow.
void registerReportView(HINSTANCE inst);
// Replaces the content of a report view and repaints.
void rvSet(HWND view, std::vector<RVItem> items);
// Action string of an item, "" when the index has none.
std::string rvActionAt(HWND view, int index);
// Marks one item as selected and clears the rest, then repaints. Used by the
// clip playlist so the card being played is obvious.
void rvHighlight(HWND view, int index);

} // namespace rlui
