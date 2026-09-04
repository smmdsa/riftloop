// Small WinHTTP wrapper. Used for Data Dragon, Riot API (optional key),
// LCU (localhost, self-signed) and Live Client Data (localhost, self-signed).
#pragma once
#include <map>
#include <string>

namespace rl::http {

struct Response {
    int         status = 0;          // 0 = transport error
    std::string body;
    std::string error;               // set when status == 0
};

struct Options {
    bool        ignoreCertErrors = false;   // required for LCU / Live Client Data
    std::string basicUser;                  // LCU: "riot"
    std::string basicPass;
    std::map<std::string, std::string> headers;
    int         timeoutMs = 8000;
};

// method: "GET", "POST", ... Body is sent as-is; set a Content-Type header
// when the endpoint needs one.
Response request(const std::string& method, const std::string& host, int port, bool https,
                 const std::string& path, const std::string& body, const Options& opt = {});

inline Response get(const std::string& host, int port, bool https,
                    const std::string& path, const Options& opt = {}) {
    return request("GET", host, port, https, path, "", opt);
}
inline Response post(const std::string& host, int port, bool https, const std::string& path,
                     const std::string& body, const Options& opt = {}) {
    return request("POST", host, port, https, path, body, opt);
}

} // namespace rl::http
