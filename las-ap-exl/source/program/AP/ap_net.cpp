// Socket thread for the AP bridge. Modeled on randovania/open-dread-rando-exlaunch remote_api.cpp (GPL-2.0).
//
// Ryujinx quirks this design works around:
// - a blocking call on any socket blocks every socket, so one thread does all socket work
// - errno is unreliable and fcntl non-blocking fails; Recv with MSG_DONTWAIT works
// - a dead peer is only noticed through a keep-alive timeout
#include "AP/ap.hpp"
#include "AP/ap_debug.hpp"
#include "nn/socket.hpp"
#include "nn/os.hpp"
#include <netinet/in.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace {
    // This game's SDK asserts on the 640 KiB pool that Dread uses; give it room.
    constexpr size_t kSocketAllocatorSize = 128 * 1024;
    constexpr size_t kSocketPoolSize = 1024 * 1024;
    // The loop sleeps 2 ms; drop a client that is silent for 10 s.
    constexpr int kKeepAliveTicks = 5000;
    constexpr size_t kMaxLine = 4096;
#ifdef LASAP_TEST
    constexpr s32 kAcceptPollMs = 2;    // the debug port is served from this loop too
#else
    constexpr s32 kAcceptPollMs = 100;
#endif

    alignas(0x1000) u8 g_ThreadStack[0x8000];
    // Static so it does not depend on the game's heap being ready this early.
    alignas(0x4000) u8 g_SocketPool[kSocketPoolSize];
    nn::os::ThreadType g_Thread;
    nn::os::MutexType g_Lock;

    std::deque<std::string> g_Inbound;
    std::deque<std::string> g_Outbound;
    std::atomic<bool> g_Connected = false;
    std::atomic<bool> g_NewConnection = false;

    s32 g_Server = -1;
    u16 g_Port = AP::PORT;
    s32 g_Client = -1;

    void Lock() { nn::os::LockMutex(&g_Lock); }
    void Unlock() { nn::os::UnlockMutex(&g_Lock); }

    // Lines this thread sends itself (HELLO, PONG), past AP::Send.
    void Queue(const std::string& line) {
        Lock();
        g_Outbound.push_back(line + "\n");
        Unlock();
    }

    bool CreateServer() {
        g_Server = nn::socket::Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_Server < 0) {
            g_Server = -1;
            return false;
        }
        int yes = 1;
        nn::socket::SetSockOpt(g_Server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = nn::socket::InetHtons(g_Port);
        if (nn::socket::Bind(g_Server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            nn::socket::Listen(g_Server, 1) < 0) {
            nn::socket::Close(g_Server);
            g_Server = -1;
            return false;
        }
        return true;
    }

    // Returns false when the connection should be dropped.
    bool SendPending() {
        Lock();
        bool ok = true;
        while (!g_Outbound.empty()) {
            const std::string& line = g_Outbound.front();
            size_t sent = 0;
            while (sent < line.size()) {
                ssize_t n = nn::socket::Send(g_Client, line.data() + sent, line.size() - sent, 0);
                if (n <= 0) {
                    ok = false;
                    break;
                }
                sent += n;
            }
            if (!ok) break;
            g_Outbound.pop_front();
        }
        Unlock();
        return ok;
    }

    void ServeClient() {
        std::string partial;
        char buf[4096];
        int keepAlive = kKeepAliveTicks;

        while (true) {
            svcSleepThread(2'000'000);

            // all that has arrived: the client's lines can outrun one read per wake (30 ghosts send ~300 lines/s)
            ssize_t n;
            while ((n = nn::socket::Recv(g_Client, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
                keepAlive = kKeepAliveTicks;
                partial.append(buf, n);
                size_t pos;
                while ((pos = partial.find('\n')) != std::string::npos) {
                    std::string line = partial.substr(0, pos);
                    partial.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) continue;
                    if (line == "PING") {
                        Queue("PONG");
                        continue;
                    }
                    Lock();
                    g_Inbound.push_back(std::move(line));
                    Unlock();
                }
                if (partial.size() > kMaxLine) partial.clear();
            }
            // Orderly shutdown by the peer.
            if (n == 0) return;

            if (!SendPending()) return;
            if (--keepAlive <= 0) return;
#ifdef LASAP_TEST
            AP::Test::NetPoll();
#endif
        }
    }

    void ThreadMain(void*) {
        Result rc = nn::socket::Initialize(g_SocketPool, kSocketPoolSize, kSocketAllocatorSize, 14);
        if (R_FAILED(rc)) {
            Logging.Log("[AP] socket init failed");
            return;
        }
#ifdef LASAP_TEST
        AP::Test::NetStart(g_Port + AP::Test::PORT_OFFSET);
#endif

        while (true) {
            if (g_Server < 0) {
                if (!CreateServer()) {
                    svcSleepThread(1'000'000'000);
                    continue;
                }
                Logging.Log("[AP] listening on " + std::to_string(g_Port));
            }
#ifdef LASAP_TEST
            AP::Test::NetPoll();
#endif
            // Accept only once a client waits: a thread blocked in Accept keeps emulators from closing the game.
            pollfd waiting{g_Server, POLLIN, 0};
            if (nn::socket::Poll(&waiting, 1, kAcceptPollMs) <= 0 || !(waiting.revents & POLLIN)) continue;

            sockaddr clientAddr{};
            u32 addrLen = sizeof(clientAddr);
            g_Client = nn::socket::Accept(g_Server, &clientAddr, &addrLen);
            if (g_Client < 0) {
                nn::socket::Close(g_Server);
                g_Server = -1;
                continue;
            }
            // lines go out as written (ghost positions are sent as they happen), not held back for an ACK
            constexpr s32 kTcpNoDelay = 1;
            s32 noDelay = 1;
            nn::socket::SetSockOpt(g_Client, IPPROTO_TCP, kTcpNoDelay, &noDelay, sizeof(noDelay));

            // HELLO goes first: what the game sends once it sees the connection comes after it
            std::string hello = AP::HelloLine() + "\n";
            Lock();
            g_Inbound.clear();
            g_Outbound.clear();
            g_Outbound.push_back(hello);
            Unlock();
            g_Connected = true;
            g_NewConnection = true;
            Logging.Log("[AP] client connected");

            ServeClient();

            g_Connected = false;
            nn::socket::Close(g_Client);
            g_Client = -1;
            Logging.Log("[AP] client disconnected");
        }
    }
}

namespace AP {
    void StartNetwork(int port) {
        g_Port = static_cast<u16>(port);
        nn::os::InitializeMutex(&g_Lock, false, 0);
        R_ABORT_UNLESS(nn::os::CreateThread(&g_Thread, ThreadMain, nullptr, g_ThreadStack, sizeof(g_ThreadStack), 16));
        nn::os::SetThreadName(&g_Thread, "APNet");
        nn::os::StartThread(&g_Thread);
    }

    bool IsClientConnected() {
        return g_Connected.load();
    }

    void Send(const std::string& line) {
#ifdef LASAP_TEST
        if (AP::Test::Capture(line)) return;  // a reply to a debug client
#endif
        if (!g_Connected.load()) return;
        Lock();
        g_Outbound.push_back(line + "\n");
        Unlock();
    }

    bool PopLine(std::string& out) {
        Lock();
        bool has = !g_Inbound.empty();
        if (has) {
            out = std::move(g_Inbound.front());
            g_Inbound.pop_front();
        }
        Unlock();
        return has;
    }

    void PushFront(const std::string& line) {
        Lock();
        g_Inbound.push_front(line);
        Unlock();
    }

    bool TakeNewConnection() {
        return g_NewConnection.exchange(false);
    }
}
