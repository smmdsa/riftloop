#include "core/ipc.h"

#include <windows.h>

#include <mutex>

namespace rl::ipc {

bool writeFrame(void* pipeHandle, const std::string& payload) {
    HANDLE h = (HANDLE)pipeHandle;
    if (payload.size() > kMaxMessage) return false;
    uint32_t len = (uint32_t)payload.size();
    DWORD written = 0;
    if (!WriteFile(h, &len, sizeof len, &written, nullptr) || written != sizeof len) return false;
    if (!WriteFile(h, payload.data(), len, &written, nullptr) || written != len) return false;
    return true;
}

bool readFrame(void* pipeHandle, std::string& payload) {
    HANDLE h = (HANDLE)pipeHandle;
    uint32_t len = 0;
    DWORD read = 0;
    if (!ReadFile(h, &len, sizeof len, &read, nullptr) || read != sizeof len) return false;
    if (len > kMaxMessage) return false;    // size cap (PRD 14.3)
    payload.resize(len);
    size_t got = 0;
    while (got < len) {
        if (!ReadFile(h, payload.data() + got, (DWORD)(len - got), &read, nullptr) || read == 0)
            return false;
        got += read;
    }
    return true;
}

// ------------------------------------------------------------------ server

struct Server::Impl {
    std::atomic<bool> running{false};
    std::thread acceptThread;
    std::mutex clientsMutex;
    std::vector<HANDLE> clients;

    void acceptLoop() {
        while (running) {
            HANDLE pipe = CreateNamedPipeW(
                kPipeName, PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES, kMaxMessage, kMaxMessage, 2000,
                nullptr);   // default DACL: creator/owner - same user only
            if (pipe == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }
            BOOL ok = ConnectNamedPipe(pipe, nullptr) ||
                      GetLastError() == ERROR_PIPE_CONNECTED;
            if (!running) { CloseHandle(pipe); break; }
            if (ok) {
                std::lock_guard lk(clientsMutex);
                clients.push_back(pipe);
            } else {
                CloseHandle(pipe);
            }
        }
    }

    void broadcast(const std::string& text) {
        std::lock_guard lk(clientsMutex);
        for (auto it = clients.begin(); it != clients.end();) {
            if (!writeFrame(*it, text)) {
                CloseHandle(*it);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }

    void stopAll() {
        running = false;
        // Wake the blocking ConnectNamedPipe with a dummy connection.
        HANDLE h = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (acceptThread.joinable()) acceptThread.join();
        std::lock_guard lk(clientsMutex);
        for (auto h2 : clients) CloseHandle(h2);
        clients.clear();
    }
};

Server::Server() : impl_(std::make_unique<Impl>()) {}
Server::~Server() { stop(); }
void Server::start() {
    if (impl_->running) return;
    impl_->running = true;
    impl_->acceptThread = std::thread([this] { impl_->acceptLoop(); });
}
void Server::stop() { if (impl_->running) impl_->stopAll(); }
void Server::broadcast(const std::string& jsonText) { impl_->broadcast(jsonText); }

// ------------------------------------------------------------------ client

struct Client::Impl {
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::thread thread;
    MessageHandler handler;

    void loop() {
        while (running) {
            HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                connected = false;
                Sleep(1500);         // reconnect cadence; no tight loop
                continue;
            }
            connected = true;
            std::string payload;
            while (running && readFrame(pipe, payload)) {
                if (handler) handler(payload);
            }
            connected = false;
            CloseHandle(pipe);
        }
    }
};

Client::Client(MessageHandler onMessage) : impl_(std::make_unique<Impl>()) {
    impl_->handler = std::move(onMessage);
}
Client::~Client() { stop(); }
void Client::start() {
    if (impl_->running) return;
    impl_->running = true;
    impl_->thread = std::thread([this] { impl_->loop(); });
}
void Client::stop() {
    impl_->running = false;
    if (impl_->thread.joinable()) impl_->thread.detach();   // blocking read; detach on exit
}
bool Client::connectedNow() const { return impl_->connected; }

} // namespace rl::ipc
