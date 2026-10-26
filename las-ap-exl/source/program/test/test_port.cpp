// The debug port (test build, TESTING.md): DBG commands and any protocol line from the test scripts, replied to on the
// same connection within milliseconds; everything the module sends the AP client is mirrored there as "AP <line>".
#ifdef LASAP_TEST
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "game/time.hpp"
#include "loggers.hpp"
#include "test/test_impl.hpp"
#include "nn/os.hpp"
#include "nn/socket.hpp"
#include <deque>
#include <netinet/in.h>

namespace AP::Test {
namespace {
    using Game::NowMs;

    constexpr int kMaxClients = 4;
    constexpr size_t kMaxLine = 4096;
    struct Client {
        s32 fd = -1;
        std::string partial;
        std::deque<std::string> out;
        u64 lastSeen = 0;
    };
    Client g_Clients[kMaxClients];
    s32 g_Listener = -1;
    int g_Port = 0;
    u64 g_ListenRetry = 0;
    nn::os::MutexType g_Lock;
    struct Inbound { int client; std::string line; };
    std::deque<Inbound> g_In;
    // The debug client whose command the game thread is handling: AP::Send replies go there.
    std::atomic<int> g_ReplyTo = -1;

    void Lock() { nn::os::LockMutex(&g_Lock); }
    void Unlock() { nn::os::UnlockMutex(&g_Lock); }

    void CloseClient(Client& c) {
        nn::socket::Close(c.fd);
        c.fd = -1;
        c.partial.clear();
        c.out.clear();
    }

    bool Listen() {
        g_Listener = nn::socket::Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_Listener < 0) return false;
        int yes = 1;
        nn::socket::SetSockOpt(g_Listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = nn::socket::InetHtons(g_Port);
        if (nn::socket::Bind(g_Listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            nn::socket::Listen(g_Listener, 2) < 0) {
            nn::socket::Close(g_Listener);
            g_Listener = -1;
            return false;
        }
        Logging.Log("[AP] debug port %d", g_Port);
        return true;
    }

    void Accept() {
        pollfd waiting{g_Listener, POLLIN, 0};
        if (nn::socket::Poll(&waiting, 1, 0) <= 0 || !(waiting.revents & POLLIN)) return;
        sockaddr addr{};
        u32 len = sizeof(addr);
        s32 fd = nn::socket::Accept(g_Listener, &addr, &len);
        if (fd < 0) return;
        // A full table drops the client that has been quiet longest (a test that was killed).
        int slot = -1;
        Lock();
        for (int i = 0; i < kMaxClients; i++) {
            if (g_Clients[i].fd < 0) { slot = i; break; }
            if (slot < 0 || g_Clients[i].lastSeen < g_Clients[slot].lastSeen) slot = i;
        }
        if (g_Clients[slot].fd >= 0) CloseClient(g_Clients[slot]);
        g_Clients[slot].fd = fd;
        g_Clients[slot].lastSeen = NowMs();
        g_Clients[slot].out.push_back(AP::HelloLine() + " debug\n");
        Unlock();
    }

    void Serve(int i) {
        Client& c = g_Clients[i];
        char buf[1024];
        ssize_t n = nn::socket::Recv(c.fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) {
            Lock();
            CloseClient(c);
            Unlock();
            return;
        }
        if (n > 0) {
            c.lastSeen = NowMs();
            c.partial.append(buf, n);
            size_t pos;
            while ((pos = c.partial.find('\n')) != std::string::npos) {
                std::string line = c.partial.substr(0, pos);
                c.partial.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                if (line == "PING") {
                    Push(i, "PONG");
                    continue;
                }
                Lock();
                g_In.push_back({i, std::move(line)});
                Unlock();
            }
            if (c.partial.size() > kMaxLine) c.partial.clear();
        }
        Lock();
        while (c.fd >= 0 && !c.out.empty()) {
            const std::string& line = c.out.front();
            size_t sent = 0;
            while (sent < line.size()) {
                ssize_t m = nn::socket::Send(c.fd, line.data() + sent, line.size() - sent, 0);
                if (m <= 0) break;
                sent += m;
            }
            if (sent < line.size()) {
                CloseClient(c);
                break;
            }
            c.out.pop_front();
        }
        Unlock();
    }

    struct Pending { int client; std::string line; };
    std::deque<Pending> g_Retry;

    // Protocol lines the AP client acts on (las/client.py), as opposed to replies to debug commands.
    bool ForClient(const std::string& line) {
        static const char* kVerbs[] = {"CHECK", "CHECKS", "HINT", "GOAL", "SIGN", "DUNGEON", "DIED", "DEATHDROPPED",
                                     "RECV", "LOG"};
        std::string verb = line.substr(0, line.find(' '));
        for (const char* v : kVerbs) {
            if (verb == v) return true;
        }
        return false;
    }
}

    void Push(int client, const std::string& line) {
        if (client < 0 || client >= kMaxClients) return;
        Lock();
        if (g_Clients[client].fd >= 0 && g_Clients[client].out.size() < 4096) g_Clients[client].out.push_back(line + "\n");
        Unlock();
    }

    void Broadcast(const std::string& line) {
        Lock();
        for (auto& c : g_Clients) {
            if (c.fd >= 0 && c.out.size() < 4096) c.out.push_back(line + "\n");
        }
        Unlock();
    }

    int ReplyTo() { return g_ReplyTo.load(); }

    void NetStart(int port) {
        g_Port = port;
        nn::os::InitializeMutex(&g_Lock, false, 0);
    }

    void NetPoll() {
        if (g_Port == 0) return;
        if (g_Listener < 0) {
            if (NowMs() < g_ListenRetry || !Listen()) {
                g_ListenRetry = NowMs() + 1000;
                return;
            }
        }
        Accept();
        for (int i = 0; i < kMaxClients; i++) {
            if (g_Clients[i].fd >= 0) Serve(i);
        }
    }

    bool Capture(const std::string& line) {
        int to = g_ReplyTo.load();
        if (to >= 0 && !ForClient(line)) {
            Push(to, line);
            return true;
        }
        // A line for the AP client: the client gets it also when a debug command caused it (a give's CHECK), so the
        // server sees what the game did; the debug client whose command it was gets it as its reply.
        Lock();
        for (int i = 0; i < kMaxClients; i++) {
            auto& c = g_Clients[i];
            if (c.fd >= 0 && c.out.size() < 4096) c.out.push_back((i == to ? "" : "AP ") + line + "\n");
        }
        Unlock();
        return false;
    }

    void Tick() {
        std::deque<Pending> lines;
        lines.swap(g_Retry);
        Lock();
        while (!g_In.empty()) {
            lines.push_back({g_In.front().client, std::move(g_In.front().line)});
            g_In.pop_front();
        }
        Unlock();
        for (auto& p : lines) {
            g_ReplyTo = p.client;
            bool done = AP::Bridge::HandleLine(p.line);
            g_ReplyTo = -1;
            if (!done && g_Retry.size() < 64) g_Retry.push_back(std::move(p));
        }
    }
}
#endif
