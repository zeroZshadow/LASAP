// The protocol's lines from the client (DESIGN.md, Game ↔ client protocol): each handled on the game thread, those
// that need a loaded save kept until there is one.
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
#include <deque>

namespace AP::Bridge {
    std::set<LocationId> g_FoundSinceLoad;

namespace {
    std::deque<std::string> g_Deferred;

    void Defer(const std::string& line) {
        // The client resends SYNC and ITEM lines, so keep one copy of each.
        if (std::find(g_Deferred.begin(), g_Deferred.end(), line) != g_Deferred.end()) return;
        if (g_Deferred.size() >= 64) g_Deferred.pop_front();
        g_Deferred.push_back(line);
    }
}

    bool HandleLine(const std::string& line) {
        Fields f(line);
        std::string_view cmd = f.Str();
        if (cmd.empty()) return true;
        if (cmd == "ITEM") {
            if (!PlayerReady()) return false;
            // ITEM <n> <key> <count> <dungeon> [<location> <item name>|<sender>]
            // -1: another player's item, or one from no location
            std::optional<int> n = f.Num<int>();
            std::string key(f.Str());
            std::optional<int> count = f.Num<int>(), dungeon = f.Num<int>(), from = f.Num<int>();
            if (!n || key.empty() || !count || !dungeon) {
                AP::Send("ERR malformed ITEM");
                return true;
            }
            int have = GetRecvCount();
            if (*n == have + 1) {
                std::optional<LocationId> location;
                if (from && *from >= 0) location = LocationId{*from};
                const KeptLocation* kept = location ? FindKept(*location) : nullptr;
                std::optional<ItemId> id = kept ? GrantKept(*kept) : GrantItem(key, *count, *dungeon);
                auto [item, sender] = AP::SplitNames(f.Rest());
                // An own item found in this save since it loaded was named by the found text already.
                bool namedWhenFound = sender.empty() && location && g_FoundSinceLoad.count(*location) != 0 &&
                                      AP::HasLocationName(*location);
                if (IsTrapKey(key)) QueueTrap(key, item, sender, sender.empty() ? location : std::nullopt);
                else if (id && !namedWhenFound) QueueShow(ItemShow{*id, item, sender});
                SetRecvCount(*n);
                have = *n;
            }
            AP::Send("RECV " + std::to_string(have));
        } else if (cmd == "SYNC") {
            if (!PlayerReady()) return false;
            AP::Send("RECV " + std::to_string(GetRecvCount()));
            SendAllChecks();
            if (EventFlags::CheckFlag(kGoalFlag) || LevelSystem::GetCurrentLevel() == kGoalLevel) AP::Send("GOAL");
            if (EventFlags::CheckFlag(AP::SIGN_FLAG)) AP::Send("SIGN");
            ReportFlags(true);
        } else if (cmd == "DEATH") {
            return ReceiveDeath(std::string(f.Rest()));
        } else if (cmd == "NAME") {
            // NAME <local id> <item name>|<player name, empty for an own item>
            std::optional<int> location = f.Num<int>();
            if (!location) {
                AP::Send("ERR malformed NAME");
                return true;
            }
            auto [item, player] = AP::SplitNames(f.Rest());
            AP::SetLocationName(LocationId{*location}, item, player);
        } else if (cmd == "CHECKED") {
            NoteServerChecks(f);
            AP::MapTracker::DataChanged();
        } else if (cmd == "INLOGIC") {
            NoteInLogic(f);
            AP::MapTracker::DataChanged();
        } else if (cmd == "DOORS") {
            NoteDoors(f);
            AP::MapTracker::DataChanged();
        } else if (cmd == "HINTED") {
            NoteHinted(f);
            AP::MapTracker::DataChanged();
        } else if (cmd == "SERVER") {
            SetServerConnected(f.Str() == "1");
        } else if (cmd == "NOTICE") {
            // NOTICE <text>: a toast in the autosave popup; '|' starts a new line
            std::string_view text = f.Rest();
            if (!text.empty()) QueueNotice(std::string(text));
        } else if (cmd == "GHOST" || cmd == "GHOSTS" || cmd == "GHOSTNAMES") {
            AP::GhostNet::HandleLine(line);
        } else if (cmd == "MSG") {
            Logging.Log("[AP] %s", std::string(f.Rest()).c_str());
#ifdef LASAP_TEST
        } else if (cmd == "DBG") {
            std::vector<std::string> a{std::string(cmd)};
            for (std::string_view field = f.Str(); !field.empty(); field = f.Str()) a.emplace_back(field);
            return AP::Test::Command(a);
#endif
        } else {
            AP::Send("ERR unknown command " + std::string(cmd));
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
        // up to 16 lines a frame, GHOST lines not counted (a crowd sends hundreds a second: TESTING.md, Measurements,
        // Ghost network; each is cheap)
        for (int i = 0, all = 0; i < 16 && all < 1024 && PopLine(line); all++) {
            if (line.compare(0, 6, "GHOST ") != 0) i++;
            if (!HandleLine(line)) Defer(line);
        }
    }
}
