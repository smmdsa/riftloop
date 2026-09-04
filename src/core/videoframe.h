// Pulling a still frame out of a clip, for playlist thumbnails.
//
// Media Foundation decodes one frame to RGB32 and hands back a 32bpp DIB the
// UI can blit. No external decoder and no WebView: the client stays native
// (PRD 14.5).
#pragma once
#include <windows.h>

#include <string>

namespace rl {

// Decodes the frame at atSec (clamped to the clip) and returns a top-down
// 32bpp bitmap scaled to fit width x height, or nullptr. The caller owns the
// bitmap and must DeleteObject it.
HBITMAP grabFrame(const std::wstring& path, double atSec, int width, int height);

// Writes a 32bpp bitmap to a PNG. Used to check that a clip really shows the
// moment it claims to show.
bool saveBitmapPng(HBITMAP bmp, const std::wstring& path);

} // namespace rl
