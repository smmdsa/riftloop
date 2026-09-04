// IPC over a named pipe (PRD 14.3, simplified for iteration 1):
// - length-prefixed JSON messages with a version field
// - message size cap, timeouts, no infinite waits
// - the Agent hosts the server; Desktop and Overlay connect as clients
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace rl::ipc {

inline constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\riftloop.agent.v1";
inline constexpr uint32_t kMaxMessage = 256 * 1024;
inline constexpr int kProtocolVersion = 1;

using MessageHandler = std::function<void(const std::string& jsonText)>;

// Agent-side broadcaster. Accepts many clients on background threads.
class Server {
public:
    Server();
    ~Server();
    void start();
    void stop();
    // A non-empty stickyKey keeps the message and replays it to every client
    // that connects later. A newer message with the same key replaces it.
    void broadcast(const std::string& jsonText, const std::string& stickyKey = "");
    void clearSticky(const std::string& stickyKey);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Desktop/Overlay-side client with auto-reconnect.
class Client {
public:
    explicit Client(MessageHandler onMessage);
    ~Client();
    void start();
    void stop();
    bool connectedNow() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Frame helpers shared by both sides (exposed for tests).
bool writeFrame(void* pipeHandle, const std::string& payload);
bool readFrame(void* pipeHandle, std::string& payload);

} // namespace rl::ipc
