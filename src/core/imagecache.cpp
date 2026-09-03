#include "core/imagecache.h"
#include "core/http.h"
#include "core/util.h"

namespace fs = std::filesystem;

namespace rl::img {

namespace {

fs::path pathFor(const std::string& kind, const std::string& id) {
    std::string safe;
    for (char c : id)
        safe += (isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_';
    return util::cacheDir() / "img" / (kind + "_" + safe + ".png");
}

} // namespace

fs::path cached(const std::string& kind, const std::string& id) {
    fs::path p = pathFor(kind, id);
    std::error_code ec;
    return fs::exists(p, ec) ? p : fs::path{};
}

fs::path ensure(const std::string& kind, const std::string& id, const std::string& url) {
    fs::path p = pathFor(kind, id);
    std::error_code ec;
    if (fs::exists(p, ec)) return p;
    if (url.rfind("https://", 0) != 0) return {};

    // Split https://host/path
    std::string rest = url.substr(8);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) return {};
    std::string host = rest.substr(0, slash);
    std::string path = rest.substr(slash);

    auto r = http::get(host, 443, true, path);
    if (r.status != 200 || r.body.empty()) return {};
    if (!util::writeFile(p, r.body)) return {};
    return p;
}

} // namespace rl::img
