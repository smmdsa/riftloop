// Embedded video player for evidence clips.
//
// Media Foundation's Media Engine renders straight into a child HWND. No
// WebView and no external player: the client stays native (PRD 14.5), and the
// clip sits next to the text it backs up instead of opening somewhere else.
#pragma once
#include <windows.h>

#include <memory>
#include <string>

namespace rlui {

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    // Attaches to a child window. Call once, before load().
    bool attach(HWND target);
    bool load(const std::wstring& path);
    void play();
    void pause();
    bool playing() const;
    void stop();                     // rewinds to the start and pauses
    void seek(double sec);
    bool ready() const;              // metadata arrived, duration is usable
    void shutdown();                 // releases the engine; the control dies with it
    // Repaints the current frame; call on WM_SIZE and WM_PAINT of the host.
    void resize(int width, int height);
    double positionSec() const;
    double durationSec() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Registers the "RiftLoopPlayer" window class. Call once before CreateWindow.
void registerPlayerView(HINSTANCE inst);

// Binds a player to its host window so the window can grow to full screen on
// its own (double click, F11) and shrink back (double click, Esc). At 430 px
// the clip is too small to read the game, which is the point of showing it.
void bindPlayerHost(HWND playerWnd, VideoPlayer* player);
void togglePlayerFullscreen(HWND playerWnd);
// Shows or hides the control bar. It is a sibling window, not a child, so the
// page switcher cannot hide it along with the rest of the page.
void showPlayerBar(HWND playerWnd, bool visible);
bool playerIsFullscreen(HWND playerWnd);

} // namespace rlui
