#include "core/http.h"
#include "core/util.h"

#include <windows.h>
#include <winhttp.h>

namespace rl::http {

namespace {

std::string base64(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 |
                     (unsigned char)in[i + 2];
        out += tbl[v >> 18]; out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63]; out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += tbl[v >> 18]; out += tbl[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8;
        out += tbl[v >> 18]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63]; out += '=';
    }
    return out;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};

} // namespace

Response get(const std::string& host, int port, bool https,
             const std::string& path, const Options& opt) {
    Response r;

    Handle session;
    session.h = WinHttpOpen(L"RiftLoop/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { r.error = "WinHttpOpen failed"; return r; }
    WinHttpSetTimeouts(session.h, opt.timeoutMs, opt.timeoutMs, opt.timeoutMs, opt.timeoutMs);

    Handle conn;
    conn.h = WinHttpConnect(session.h, util::widen(host).c_str(), (INTERNET_PORT)port, 0);
    if (!conn.h) { r.error = "WinHttpConnect failed"; return r; }

    Handle req;
    req.h = WinHttpOpenRequest(conn.h, L"GET", util::widen(path).c_str(), nullptr,
                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               https ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) { r.error = "WinHttpOpenRequest failed"; return r; }

    if (https && opt.ignoreCertErrors) {
        // The LCU and Live Client Data endpoints use a self-signed local cert.
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(req.h, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof flags);
    }

    std::wstring headers;
    for (auto& [k, v] : opt.headers)
        headers += util::widen(k) + L": " + util::widen(v) + L"\r\n";
    if (!opt.basicUser.empty()) {
        headers += L"Authorization: Basic " +
                   util::widen(base64(opt.basicUser + ":" + opt.basicPass)) + L"\r\n";
    }

    BOOL ok = WinHttpSendRequest(req.h,
                                 headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                 headers.empty() ? 0 : (DWORD)-1,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!ok || !WinHttpReceiveResponse(req.h, nullptr)) {
        r.error = "request failed, code " + std::to_string(GetLastError());
        return r;
    }

    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        std::string chunk(avail, 0);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, chunk.data(), avail, &read)) break;
        chunk.resize(read);
        r.body += chunk;
        if (r.body.size() > 64u * 1024 * 1024) { r.error = "response too large"; break; }
    }
    return r;
}

} // namespace rl::http
