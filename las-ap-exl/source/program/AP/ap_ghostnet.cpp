// Multiplayer ghosts, the network half (DESIGN.md, Ghosts): while the client asks for it (GHOSTS 1), Link's position
// goes to the client as POS lines, which it relays to the other players; their positions come back as GHOST lines,
// kept here per ghost slot for the puppets.
#include "AP/ap.hpp"
#include "Game/framework.hpp"
#include "Hooks/Common/level.hpp"
#include "Game/Data/inventory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
    // 20 Hz while anything changes, a keep-alive at 2 Hz while Link stands still. What the player does goes out at
    // once: an animation starting, a turn of kTurnDeg or more, starting or stopping to move; an animation's frame
    // running on is no change (the receiver plays it on). Each line carries the time it was taken: the receiver
    // replays them on that timeline, so their delivery may be uneven.
    constexpr u64 kMovingMs = 50;
    constexpr u64 kStillMs = 500;
    constexpr float kTurnDeg = 10;
    // A jump this far between two sent positions is a warp (respawn, warp, door within a level): the ghost snaps.
    constexpr float kWarpDistance = 4.0f;
    constexpr int kFlagWarped = 1, kFlagStill = 4, kFlagStarted = 8;
    // Link's animations (game 1.0.0): the player's animation controller (player +0x1568) plays rows of
    // PlayerAnimation.gsheet by key (the row index; its fileName names the FSKA in Link's model). Its slot entries
    // (+0x78, 0x18 apart) hold the row playing (+0x10); slot 0 is the whole body. The frame is in the controller's
    // skeletal animation component (+0x8): layer i (+0x1d0 array, count +0x1c8) is slot i, its bound animation
    // (+0x8) -> +0 -> +0x8 the frame control (f32 frame at +0). The sheet: **(0x1cbaf70) (a shared_ptr), +0x70 -> data, rows at +0x20
    // (0x48 apart), count +0x28; a row: s32 key, +0x8 fileName, +0x1c policy (1 loops).
    constexpr size_t kPlayerAnimCtrl = 0x1568, kCtrlSac = 0x8, kCtrlSlots = 0x78, kSlotRow = 0x10;
    constexpr size_t kSacLayerCount = 0x1c8, kSacLayers = 0x1d0, kLayerAnim = 0x8, kAnimFrameCtrl = 0x8;
    constexpr uintptr_t kPlayerAnimSheet = 0x1cbaf70;
    constexpr size_t kSheetData = 0x70, kDataRows = 0x20, kDataCount = 0x28, kRowSize = 0x48, kRowName = 0x8,
                     kRowPolicy = 0x1c;
    // Link's look: his model component (player +0x2a8) -> model instance (+0x148) -> one byte per shape (+0x378), 1
    // drawn (Player_ShowItem shows the items he holds this way); the shape count is the g3d model object's (instance
    // +0x60 -> +0x10, +0x30). His tunic is the inventory's upgrade bits (+0x9c): 6 red, 7 blue, red first.
    constexpr size_t kPlayerModelComp = 0x2a8, kModelCompInstance = 0x148, kInstanceShapes = 0x378,
                     kInstanceModel = 0x60, kModelObject = 0x10, kModelShapeCount = 0x30;
    constexpr size_t kInventoryUpgrades = 0x9c;
    constexpr u8 kUpgradeRed = 0x40, kUpgradeBlue = 0x80;

    struct Sample {
        bool inLevel = false;
        std::string level;
        int room = 255;
        float x = 0, y = 0, z = 0, yaw = 0;
        int anim = 0, animFrame = 0;  // AP::GhostNet::Anim
        u32 starts = 0;
        bool still = false;           // not moved since the previous frame
        u32 look = AP::GhostNet::LOOK_DEFAULT;
    };

    bool g_Wanted = false;  // the client relays positions (GHOSTS 1)
    bool g_Names = true;    // name tags over the ghosts (GHOSTNAMES 1)
    Sample g_Sent;
    bool g_SentAny = false;
    u64 g_SentAt = 0;
    // Link's position in the previous frame, for still
    u32 g_Frame = 0;
    bool g_PrevValid = false, g_Still = false;
    float g_PrevX = 0, g_PrevY = 0, g_PrevZ = 0;
    AP::GhostNet::Anim g_Anim;
    const void* g_AnimRow = nullptr;
    u32 g_Look = AP::GhostNet::LOOK_DEFAULT;
#ifdef LASAP_TEST
    // DBG ghostnet: Link's animation starts and the GHOST lines with a start, by frame
    std::vector<std::string> g_StartLog;
    u32 FrameNow() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr ? fmwk->mFrameCount : 0;
    }
#endif
    u32 g_PosLines = 0;
    std::string g_LastPos;
    AP::GhostNet::Ghost g_Ghosts[AP::GhostNet::SLOTS];
    u32 g_GhostLines = 0;

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    std::vector<std::string> Split(const std::string& line, size_t max) {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos < line.size() && out.size() < max) {
            size_t start = line.find_first_not_of(' ', pos);
            if (start == std::string::npos) break;
            size_t end = line.find(' ', start);
            if (end == std::string::npos || out.size() + 1 == max) end = line.size();
            out.push_back(line.substr(start, end - start));
            pos = end;
        }
        return out;
    }

    // The overworld room around a position, (row - 1) * 16 + column (Field_<row><col>, col A = 0), as the relay
    // computes it; 255 outside Field.
    int RoomIndex(const std::string& level, float x, float z) {
        if (level != "Field") return 255;
        int col = std::min(15, std::max(0, static_cast<int>(std::floor(x / 15))));
        int row = std::min(15, std::max(0, static_cast<int>(std::floor(z / 12))));
        return row * 16 + col;
    }

    template<typename T>
    T At(const void* base, size_t offset) {
        return base != nullptr ? *reinterpret_cast<const T*>(reinterpret_cast<const u8*>(base) + offset) : T{};
    }

    // The row playing on slot 0 of Link's animation controller and its frame, or nullptr.
    const u8* LinkAnimRow(const void* player, float& frame) {
        const u8* ctrl = At<const u8*>(player, kPlayerAnimCtrl);
        const u8* slots = At<const u8*>(ctrl, kCtrlSlots);
        if (slots == nullptr || slots == At<const u8*>(ctrl, kCtrlSlots + 8)) return nullptr;
        const u8* row = At<const u8*>(slots, kSlotRow);
        const u8* sac = At<const u8*>(ctrl, kCtrlSac);
        const u8* fc = nullptr;
        if (At<s32>(sac, kSacLayerCount) > 0) {
            const u8* layer = At<const u8*>(At<const u8*>(sac, kSacLayers), 0);
            fc = At<const u8*>(At<const u8*>(At<const u8*>(layer, kLayerAnim), 0), kAnimFrameCtrl);
        }
        frame = At<float>(fc, 0);
        if (!std::isfinite(frame) || frame < 0) frame = 0;
        return row;
    }

    // Once a frame: Link's animation, and whether one started (another row, or the same one from its start again
    // unless it loops).
    void ReadAnim(const void* player) {
        float frame = 0;
        const u8* row = LinkAnimRow(player, frame);
        int anim = row != nullptr ? At<s32>(row, 0) + 1 : 0;
        float prevFrame = g_Anim.frame / 10.0f;
        if (anim != g_Anim.anim || row != g_AnimRow ||
            (anim != 0 && frame + 0.5f < prevFrame && At<s32>(row, kRowPolicy) != 1)) {
            if (anim != 0) g_Anim.starts++;
#ifdef LASAP_TEST
            const char* name = anim != 0 ? At<const char*>(row, kRowName) : nullptr;
            if (name != nullptr && g_StartLog.size() < 60) g_StartLog.push_back(std::string(name) + "@" + std::to_string(FrameNow()));
#endif
        }
        g_AnimRow = row;
        g_Anim.anim = anim;
        g_Anim.frame = std::min(static_cast<int>(frame * 10), 0xffff);
    }

    u32 ReadLook(const void* player) {
        const u8* instance = At<const u8*>(At<const u8*>(player, kPlayerModelComp), kModelCompInstance);
        const u8* shapes = At<const u8*>(instance, kInstanceShapes);
        const u8* model = At<const u8*>(At<const u8*>(instance, kInstanceModel), kModelObject);
        int count = std::min<int>(At<u16>(model, kModelShapeCount), AP::GhostNet::LOOK_SHAPES);
        if (shapes == nullptr || count == 0) return AP::GhostNet::LOOK_DEFAULT;
        u32 look = 0;
        for (int i = 0; i < count; i++)
            if (shapes[i] != 0) look |= 1u << i;
        u8 upgrades = At<u8>(*Game::Data::Inventory::Items, kInventoryUpgrades);
        u32 tunic = (upgrades & kUpgradeRed) ? 1 : (upgrades & kUpgradeBlue) ? 2 : 0;
        return look | tunic << AP::GhostNet::LOOK_TUNIC_SHIFT;
    }

    Sample Current() {
        Sample s;
        Game::Framework* fmwk = Game::GetFramework();
        // no player while a level loads
        if (fmwk == nullptr || fmwk->mPlayer == nullptr || fmwk->mPlayer->playerCollision == nullptr) return s;
        const Game::Coords& c = fmwk->mPlayer->playerCollision->coordsNew;
        if (!std::isfinite(c.pos.x) || !std::isfinite(c.pos.y) || !std::isfinite(c.pos.z)) return s;
        s.inLevel = true;
        s.level = LevelSystem::GetCurrentLevel();
        s.x = c.pos.x;
        s.y = c.pos.y;
        s.z = c.pos.z;
        // Link turns about the vertical axis: yaw in degrees, 0 = +z (the bottom of the screen), 90 = +x
        const Game::Vec4& q = c.rotate;
        s.yaw = std::atan2(2 * (q.x * q.z + q.t * q.y), 1 - 2 * (q.x * q.x + q.y * q.y)) * 180.0f / 3.14159265f;
        if (s.yaw < 0) s.yaw += 360;
        s.room = RoomIndex(s.level, s.x, s.z);
        // AP::Tick runs once per input poll, which can be more than once a frame
        if (fmwk->mFrameCount != g_Frame) {
            g_Still = g_PrevValid && std::fabs(s.x - g_PrevX) < 0.001f && std::fabs(s.y - g_PrevY) < 0.001f &&
                      std::fabs(s.z - g_PrevZ) < 0.001f;
            g_Frame = fmwk->mFrameCount;
            g_PrevValid = true;
            g_PrevX = s.x;
            g_PrevY = s.y;
            g_PrevZ = s.z;
            ReadAnim(fmwk->mPlayer);
            g_Look = ReadLook(fmwk->mPlayer);
        }
        s.still = g_Still;
        s.anim = g_Anim.anim;
        s.animFrame = g_Anim.frame;
        s.starts = g_Anim.starts;
        s.look = g_Look;
        return s;
    }

    float Turn(const Sample& a, const Sample& b) {
        float turn = std::fabs(a.yaw - b.yaw);
        return std::min(turn, 360 - turn);
    }

    // What the player did, sent at once: another level, animation, facing or look, or starting or stopping.
    bool Acted(const Sample& a, const Sample& b) {
        if (a.inLevel != b.inLevel || a.level != b.level) return true;
        return a.inLevel && (a.starts != b.starts || a.still != b.still || a.look != b.look || Turn(a, b) >= kTurnDeg);
    }

    bool Changed(const Sample& a, const Sample& b) {
        if (a.inLevel != b.inLevel || a.level != b.level) return true;
        if (!a.inLevel) return false;
        if (a.starts != b.starts || a.still != b.still || a.look != b.look) return true;
        return std::fabs(a.x - b.x) > 0.01f || std::fabs(a.y - b.y) > 0.01f || std::fabs(a.z - b.z) > 0.01f ||
               Turn(a, b) > 1.0f;
    }

    void SendPos(const Sample& s, u64 now) {
        int flags = 0;
        if (s.inLevel && (!g_SentAny || !g_Sent.inLevel || g_Sent.level != s.level ||
                          std::hypot(s.x - g_Sent.x, s.y - g_Sent.y, s.z - g_Sent.z) > kWarpDistance)) {
            flags |= kFlagWarped;
        }
        if (s.still) flags |= kFlagStill;
        if (g_SentAny && s.starts != g_Sent.starts) flags |= kFlagStarted;
        char buf[160];
        if (s.inLevel) {
            // POS <level> <room> <x> <y> <z> <yaw> <anim> <anim frame> <flags> <time ms> <look>
            snprintf(buf, sizeof(buf), "POS %s %d %.3f %.3f %.3f %.1f %d %d %d %u %u", s.level.c_str(), s.room, s.x,
                     s.y, s.z, s.yaw, s.anim, s.animFrame, flags, static_cast<u32>(now), s.look);
        } else {
            snprintf(buf, sizeof(buf), "POS none");
        }
        g_LastPos = buf;
        AP::Send(g_LastPos);
        g_Sent = s;
        g_SentAny = true;
        g_SentAt = now;
        g_PosLines++;
    }
}

namespace AP::GhostNet {
    void Tick() {
        Sample s = Current();  // reads Link's animation once a frame (LinkAnim), sent or not
        if (!g_Wanted || !AP::IsClientConnected()) return;
        u64 now = NowMs();
        bool acted = g_SentAny && Acted(s, g_Sent);
        if (now - g_SentAt < kMovingMs && !acted) return;
        if (!g_SentAny || Changed(s, g_Sent) || now - g_SentAt >= kStillMs) SendPos(s, now);
    }

    void Reset() {
        g_Wanted = false;
        g_Names = true;
        g_SentAny = false;
        for (auto& ghost : g_Ghosts) ghost.active = false;
    }

    u64 g_HandleTicks = 0;  // time spent on GHOST lines (system ticks, 19.2 MHz), for DBG ghostnet

    void Handle(const std::string& line);

    void HandleLine(const std::string& line) {
        u64 t0, t1;
        asm volatile("mrs %0, cntpct_el0" : "=r"(t0));
        Handle(line);
        asm volatile("mrs %0, cntpct_el0" : "=r"(t1));
        g_HandleTicks += t1 - t0;
    }

    void Handle(const std::string& line) {
        auto a = Split(line, 13);
        if (a[0] == "GHOSTS" && a.size() >= 2) {
            // GHOSTS 1|0: whether the client relays positions; with 0 no ghost shows
            g_Wanted = a[1] == "1";
            g_SentAny = false;
            if (!g_Wanted) {
                for (auto& ghost : g_Ghosts) ghost.active = false;
            }
            return;
        }
        if (a[0] == "GHOSTNAMES" && a.size() >= 2) {
            // GHOSTNAMES 1|0: whether name tags show over the ghosts
            g_Names = a[1] == "1";
            return;
        }
        if (a[0] != "GHOST" || a.size() < 3) return;
        int n = atoi(a[1].c_str());
        if (n < 0 || n >= SLOTS) return;
        Ghost& ghost = g_Ghosts[n];
        g_GhostLines++;
        if (a[2] == "off") {
            // GHOST <n> off: that player left this area or the run
            ghost.active = false;
            return;
        }
        if (a.size() < 13) return;
        // GHOST <n> <level> <x> <y> <z> <yaw> <anim> <anim frame> <flags> <time> <look> <slot name>; time is the
        // sender's clock (ms, 16 bits): unwrapped against the slot's previous line, a new timeline after an off
        u16 time = static_cast<u16>(strtoul(a[10].c_str(), nullptr, 10));
        if (ghost.active && ghost.frames > 0) {
            ghost.timeMs += static_cast<s16>(static_cast<u16>(time - static_cast<u16>(ghost.timeMs)));
        } else {
            ghost.timeMs = 0x10000 + time;
            ghost.timelineFrom = ghost.frames;
        }
        ghost.active = true;
        ghost.level = a[2];
        ghost.x = strtof(a[3].c_str(), nullptr);
        ghost.y = strtof(a[4].c_str(), nullptr);
        ghost.z = strtof(a[5].c_str(), nullptr);
        ghost.yaw = strtof(a[6].c_str(), nullptr);
        ghost.anim = atoi(a[7].c_str());
        ghost.animFrame = atoi(a[8].c_str());
        ghost.flags = atoi(a[9].c_str());
        ghost.look = static_cast<u32>(strtoul(a[11].c_str(), nullptr, 10));
        ghost.name = a[12];
        ghost.receivedMs = NowMs();
#ifdef LASAP_TEST
        if ((ghost.flags & 8) && g_StartLog.size() < 60) {
            const char* name = AnimName(ghost.anim);
            g_StartLog.push_back("rx:" + std::string(name != nullptr ? name : "?") + "@" + std::to_string(FrameNow()));
        }
#endif
        ghost.history[ghost.frames % HISTORY] = Frame{ghost.timeMs, ghost.receivedMs, ghost.x, ghost.y, ghost.z,
                                                      ghost.yaw, ghost.anim, ghost.animFrame, ghost.flags,
                                                      ghost.look};
        ghost.frames++;
    }

    const Ghost& Get(int n) {
        return g_Ghosts[n];
    }

    bool ShowNames() {
        return g_Names;
    }

    Anim LinkAnim() {
        return g_Anim;
    }

    u32 LinkLook() {
        return g_Look;
    }

    const char* AnimName(int anim) {
        const u8* holder = At<const u8*>(reinterpret_cast<const void*>(g_BaseAddress + kPlayerAnimSheet), 0);
        const u8* sheet = At<const u8*>(holder, 0);
        const u8* data = At<const u8*>(sheet, kSheetData);
        if (anim <= 0 || static_cast<u32>(anim) > At<u32>(data, kDataCount)) return nullptr;
        return At<const char*>(At<const u8*>(data, kDataRows) + (anim - 1) * kRowSize, kRowName);
    }

    std::string State() {
        std::string s = "GHOSTNET wanted=" + std::to_string(g_Wanted) + " names=" + std::to_string(g_Names) + " pos=" + std::to_string(g_PosLines) +
                        " ghostlines=" + std::to_string(g_GhostLines) + " handle_us=" + std::to_string(g_HandleTicks * 10 / 192) +
                        " last=" + g_LastPos;
        for (int n = 0; n < SLOTS; n++) {
            const Ghost& g = g_Ghosts[n];
            if (!g.active) continue;
            char buf[200];
            const char* anim = AnimName(g.anim);
            snprintf(buf, sizeof(buf), " | %d %s %s %.2f,%.2f,%.2f yaw=%.0f anim=%s flags=%d look=%x frames=%u", n,
                     g.name.c_str(), g.level.c_str(), g.x, g.y, g.z, g.yaw, anim != nullptr ? anim : "-", g.flags,
                     g.look, g.frames);
            s += buf;
        }
#ifdef LASAP_TEST
        if (!g_StartLog.empty()) {
            s += " | starts";
            for (auto& e : g_StartLog) s += " " + e;
            g_StartLog.clear();
        }
#endif
        return s;
    }
}
