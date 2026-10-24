// The protocol's lines from the client (DESIGN.md, Protocol): each handled on the game thread, those that need a
// loaded save kept until there is one.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_text.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/hooks/level.hpp"
#include "ghost/ghost_net.hpp"
#include "loggers.hpp"
#include "map/map_tracker.hpp"
#ifdef LASAP_TEST
#include "test/test.hpp"
#endif
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <sstream>

namespace AP::Bridge {
    std::set<int> g_FoundSinceLoad;

namespace {
    std::deque<std::string> g_Deferred;

    void Defer(const std::string& line) {
        // The client resends SYNC and ITEM lines, so keep one copy of each.
        if (std::find(g_Deferred.begin(), g_Deferred.end(), line) != g_Deferred.end()) return;
        if (g_Deferred.size() >= 64) g_Deferred.pop_front();
        g_Deferred.push_back(line);
    }

    std::vector<std::string> Split(const std::string& line) {
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string p;
        while (ss >> p) parts.push_back(p);
        return parts;
    }

    // The text after the first n space-separated fields of a line.
    std::string RestAfter(const std::string& line, int n) {
        size_t pos = 0;
        for (int i = 0; i < n && pos != std::string::npos; i++) {
            pos = line.find_first_not_of(' ', pos);
            pos = pos == std::string::npos ? pos : line.find(' ', pos);
        }
        return pos == std::string::npos ? "" : line.substr(pos + 1);
    }
}

    // Returns false if the line must be retried on a later frame.
    bool HandleLine(const std::string& line) {
        auto a = Split(line);
        if (a.empty()) return true;
        if (a[0] == "ITEM" && a.size() >= 5) {
            if (!PlayerReady()) return false;
            int n = atoi(a[1].c_str());
            int have = GetRecvCount();
            if (n == have + 1) {
                // ITEM <n> <key> <count> <dungeon> [<location> <item name>|<sender>]
                int location = a.size() >= 6 ? atoi(a[5].c_str()) : -1;
                const KeptLocation* kept = FindKept(location);
                int id = kept ? GrantKept(*kept) : GrantItem(a[2], atoi(a[3].c_str()), atoi(a[4].c_str()));
                std::string names = RestAfter(line, 6);
                size_t bar = names.find('|');
                std::string item = bar == std::string::npos ? names : names.substr(0, bar);
                std::string sender = bar == std::string::npos ? "" : names.substr(bar + 1);
                // An own item found in this save since it loaded was named by the found text already.
                bool namedWhenFound = sender.empty() && g_FoundSinceLoad.count(location) != 0 &&
                                      AP::HasLocationName(location);
                if (IsTrapKey(a[2])) QueueTrap(a[2], item, sender);
                else if (id >= 0 && !namedWhenFound) QueueShow(Show{id, item, sender, ""});
                SetRecvCount(n);
                have = n;
            }
            AP::Send("RECV " + std::to_string(have));
        } else if (a[0] == "SYNC") {
            if (!PlayerReady()) return false;
            AP::Send("RECV " + std::to_string(GetRecvCount()));
            SendAllChecks();
            if (EventFlags::CheckFlag(kGoalFlag) || LevelSystem::GetCurrentLevel() == kGoalLevel) AP::Send("GOAL");
            if (EventFlags::CheckFlag(AP::SIGN_FLAG)) AP::Send("SIGN");
            ReportHints(true);
        } else if (a[0] == "DEATH") {
            return ReceiveDeath(RestAfter(line, 1));
        } else if (a[0] == "NAME" && a.size() >= 2) {
            // NAME <local id> <item name>|<player name, empty for an own item>
            std::string names = RestAfter(line, 2);
            size_t bar = names.find('|');
            AP::SetLocationName(atoi(a[1].c_str()), names.substr(0, bar),
                                bar == std::string::npos ? "" : names.substr(bar + 1));
        } else if (a[0] == "CHECKED") {
            NoteServerChecks(a);
            AP::MapTracker::DataChanged();
        } else if (a[0] == "INLOGIC") {
            NoteInLogic(a);
            AP::MapTracker::DataChanged();
        } else if (a[0] == "SERVER" && a.size() >= 2) {
            SetServerConnected(a[1] == "1");
        } else if (a[0] == "NOTICE" && a.size() >= 2) {
            // NOTICE <text>: a toast in the autosave popup; '|' starts a new line
            QueueNotice(RestAfter(line, 1));
        } else if (a[0] == "GHOST" || a[0] == "GHOSTS" || a[0] == "GHOSTNAMES") {
            AP::GhostNet::HandleLine(line);
        } else if (a[0] == "MSG") {
            Logging.Log("[AP] %s", line.c_str() + 4);
#ifdef LASAP_TEST
        } else if (a[0] == "DBG") {
            return AP::Test::Command(a);
#endif
        } else {
            AP::Send("ERR unknown command " + a[0]);
        }
        return true;
    }

    void PumpLines() {
        // Lines that need a loaded save wait in g_Deferred; everything else runs right away.
        if (!g_Deferred.empty() && PlayerReady()) {
            std::deque<std::string> retry;
            retry.swap(g_Deferred);
            for (const auto& line : retry) {
                if (!HandleLine(line)) Defer(line);
            }
        }
        std::string line;
        // up to 16 lines a frame, GHOST lines not counted (30 ghosts send up to 600 a second; each is cheap)
        for (int i = 0, all = 0; i < 16 && all < 1024 && PopLine(line); all++) {
            if (line.compare(0, 6, "GHOST ") != 0) i++;
            if (!HandleLine(line)) Defer(line);
        }
    }
}
