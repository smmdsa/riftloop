// Local icon cache for Data Dragon images (champions, items, runes).
// Files land in cache\img\<kind>_<id>.png. Download happens once; readers
// (Overlay, Desktop) only touch local files.
#pragma once
#include <filesystem>
#include <string>

namespace rl::img {

// Returns the local path when the file exists or the download succeeds;
// empty path on error. Never throws.
std::filesystem::path ensure(const std::string& kind, const std::string& id,
                             const std::string& url);

// Local path without downloading; empty when not cached yet.
std::filesystem::path cached(const std::string& kind, const std::string& id);

} // namespace rl::img
