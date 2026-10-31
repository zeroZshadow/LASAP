#pragma once
// The puppets' parts, shared by the ghost_*.cpp files that make them and by the test build's DBG puppet
// (test_puppets.cpp). See ghost_puppets.cpp for the whole.
#include "ghost/ghost_puppets.hpp"
#include "game/actor.hpp"
#include "game/actors/npc.hpp"
#include "game/model.hpp"
#include "game/stl.hpp"
#include "ghost/ghost_net.hpp"
#include "lib.hpp"
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace Game::Actors { struct Player; }

namespace AP::Puppets {
    // ---- Actors (ghost_puppets.cpp) ----

    // The look's shapes: the animator's model instance's shapeVisible. Nothing but the model's creation and the
    // shape setters writes them.
    // The face: Link's eyes, mouth and ears (ghost_look.cpp, SetFace).
    constexpr int kFaceParts = 3;
    constexpr u32 kFaceShapes = 0x380;  // the ear, eyes and mouth

    // ---- Replay (ghost_replay.cpp) ----

    constexpr int kTrack = 32;
    // The lines whose lateness sets the buffer (ghost_replay.cpp).
    constexpr int kLateLines = 128;
    using Frame = AP::GhostNet::Frame;
    struct Track {
        Frame frames[kTrack];  // newest at frames[(count - 1) % kTrack]
        u32 count = 0;
        double offset = 0;     // this clock minus the sender's (ms), the least seen
        float late[kLateLines] = {};  // each line's delay past offset (ms), the newest at late[(count - 1) % kLateLines]
        double target = 0;     // the buffer the lines' lateness asks for (ms)
        double buffer = 0;     // the buffer the replay runs at (ms), moving towards target; 0: not set yet
        int gait = -1;         // index in kGaits, -1: none played yet
        int anim = -1;         // the sender's animation id playing, -1: none played yet
        u32 passed = 0;        // the frames (count) below this one the replay has reached: their starts looked at
        Game::Vec3 pos{};
        Game::Vec3 error{};    // added to the replay's position, fading out (kCorrectMs)
        u32 replayed = 0;      // count at the last frame
        u32 arrival = 0;       // the frame (count) of the last warped frame + 1, 0: none waiting (ArrivalHeld)
        bool arriving = false; // the replay holds the ghost hidden at its arrival
        double lastNow = 0;
        float moved[60] = {};  // speed in each of the last 60 frames (DBG puppet: smoothness)
        u32 shownFrames = 0;
    };
    // The least and the most a replay runs behind the newest frame (on the sender's clock; a ghost whose lines arrive
    // unevenly runs further behind), and the blend time of the sender's animations (DBG puppet buffer, blend).
    extern double g_BufferMs, g_BufferMaxMs;
    extern float g_AnimBlend;
    constexpr float kGaitBlend = 0.1f;
    using AP::GhostNet::GhostFlag;
    // The animations a sender that sends none plays by speed.
    constexpr const char* kGaits[] = {"wait", "walk", "run", "dash_lp"};

    // ---- Puppets ----

    // Puppets 0..SLOTS-1 show GhostNet's ghost slots; the last is DBG puppet's.
#ifdef LASAP_TEST
    constexpr int kGhosts = AP::GhostNet::SLOTS, kTest = kGhosts, kPuppets = kGhosts + 1;
#else
    constexpr int kGhosts = AP::GhostNet::SLOTS, kPuppets = kGhosts;
#endif

    struct Puppet {
        Game::Weak<Game::Actor> actor;  // an inactive actor (Entity::Active) is neither updated nor drawn
        Game::ModelObj* model = nullptr;  // once loaded
        AP::GhostNet::Look look;                 // the look of the frame replayed
        int tunic = -1;                          // the tunic set, -1: none yet, kNoTunic: the model has none
        void* tunicAnim = nullptr;               // the animation on the tunic's slot when it was set
        int upper = -1;                          // the anim id on the upper body's layer, -1: not set on this model
        int faceSlots[kFaceParts] = {-1, -1, -1};  // kFaceAnims' slots, -1: not found (yet)
        int faceSet[kFaceParts] = {-1, -1, -1};    // the frames set on them
        bool ownFace = false;                    // draws its own face shapes (another face than the instanced one)
        bool shown = false;
        u64 hiddenSince = 0;     // ms
        u32 spawnAfter = 0;      // frame
        Track track;
        u32 framesSeen = 0;      // the ghost's lines fed so far
        u32 inactiveFrames = 0;  // shown with an inactive actor
        bool wasActive = false;  // its actor has been active
        int detail = 2;          // 2: its whole look drawn, 1: only kCrowdShapes, 0: none (crowds, Rank)
        u32 shownMats[4] = {};   // the model's material visibility words a ghost draws with (kShownMaterials)
        int matWords = -1;       // their count, -1: not computed for this model
        u32 readyFrames = 0;     // frames its body has drawn in a row (visibility kept, actor active)
        bool ready = false;      // its body draws: its name tag and instanced shapes may show
#ifdef LASAP_TEST
        u32 visFixes = 0;        // frames whose visibility the module had to write again
        std::vector<std::string> animLog;  // its plays and model loads, for DBG puppet animlog
        u32 shownAt = 0;                   // frame it was last shown
        // its replay since DBG puppet stats reset (DBG puppet stats)
        struct {
            u32 frames = 0, held = 0, past = 0, catchup = 0, snaps = 0, speeds = 0;
            double speed = 0, speedSq = 0, buffer = 0, bufferMax = 0;
        } stats;
#endif
    };
    extern Puppet g_Puppets[kPuppets];
    extern u32 g_LastFrame;  // the game's frame count at the last frame the puppets moved

    // Knobs for DBG puppet: name tags shown, the ghosts' models drawn, the look's shapes they may draw, new ghost
    // models given the layered animation player; and the actors made again.
    extern bool g_NameTags, g_DrawGhosts, g_LayeredGhosts;
    extern u32 g_ShapeMask;
    extern u32 g_Remade;

    int IndexOf(const Puppet& p);
    bool Alive(const Puppet& p);
    // The puppet's NPC component (NpcGhost's), null without a live actor. Its animator plays animations by name
    // (ghost_puppets.cpp, PlayAnim): the NPC's SkeletalAnimation, or the LayeredAnimation the module gives ghosts.
    Game::Actors::NpcComponent* NpcOf(const Puppet& p);
    void* AnimatorOf(const Puppet& p);
    bool IsLayered(const void* animator);
    Game::ModelInstance* AnimatorInstance(const void* animator);
    Game::ModelObj* AnimatorModel(const void* animator);
    Game::SkeletalModelComponent* AnimatorModelComp(const void* animator);
    // Creates actor <id> at pos as PopEnemy does and keeps a weak reference to it; a puppet that has an actor shows it
    // there again.
    bool Spawn(Puppet& p, u16 id, const Game::Vec3& pos, u32 frame, std::string& err);
    // Has the game destroy the actor (at the entity manager's next flush) and lets it go.
    void Remove(Puppet& p);
    void Place(Puppet& p, const Game::Vec3& pos, const Game::Vec4* rot);
    // Plays an animation of the puppet's model by name from a frame.
    void PlayAnim(Puppet& p, const char* name, float blend, float frame);
    // The sender's upper-body animation (anim id, 0 none) on layer 1 of the puppet's layered animation player.
    void SetUpper(Puppet& p, int anim);
    Game::Vec4 QuatOf(float yaw);

    // ---- Replay (ghost_replay.cpp) ----

    void ResetTrack(Track& tr);
    void Push(Track& tr, const Frame& f);
    // One frame of the replay; picks the animation from the speed.
    void ReplayFrame(Puppet& p, double now);

    // ---- Look (ghost_look.cpp) ----

    constexpr int kNoTunic = -2;
    // Draws only the materials a ghost shows, or (hidden) none; true when they already were.
    bool WriteLook(Puppet& p, bool draw);
    void SetShapes(Puppet& p);
    Game::MatAnimSlot* TunicSlot(const Puppet& p, int& index);
    void SetTunic(Puppet& p);
    void SetFace(Puppet& p);

    // ---- Dither (ghost_dither.cpp) ----

    // Each shown puppet's blocks as GPU addresses [lo, hi), for the render thread's hooks; hi 0: none. One per puppet
    // and one per instanced model.
    struct Span { std::atomic<u64> lo{0}, hi{0}; };
    constexpr int kInstSpan = kPuppets, kInstSpans = 9;
    extern Span g_Spans[kPuppets + kInstSpans];
    // The render thread's command buffer wrappers and their contexts; the ghost draws dithered, of them the instanced
    // models', and the puppet draws per wrapper (in g_Contexts' order).
    extern std::pair<void*, u8*> g_Contexts[8];
    extern u32 g_GhostDraws, g_InstDraws;
    extern u32 g_DrawsBy[8];
    // DBG puppet dither, skip: the dither state, and the wrappers whose puppet draws are dropped.
    extern bool g_Dither;
    extern u32 g_SkipMask;
    void ClearSpan(int n);
    // The model's blocks (its materials' and shapes') as GPU addresses, for the render thread.
    void PublishSpan(int n, const Game::ModelObj* model);
    void InstallDitherHooks();

    // ---- Instanced equipment and faces (ghost_instanced.cpp) ----

    // The shapes the instanced models draw: the puppets draw none of them.
    constexpr u32 kInstancedShapes = 0xff387;
    // The head bone, where the face and the name tag go.
    constexpr int kHeadBone = 28;
    struct Inst {
        Game::InstancedModelComponent* comp = nullptr;
        void* host = nullptr;
        u32 drawn = 0;  // records written last frame
    };
    constexpr int kInstGroupCount = kInstSpans;
    extern Inst g_Inst[kInstGroupCount];
    const float* BoneWorld(const Game::ModelObj* model, int bone);
    void InstDropAll(bool hostAlive);
    // Each frame: the instanced models' records, one per drawn ghost.
    void InstTick();

    // ---- Crowds (ghost_crowd.cpp) ----

    // Shapes of ghosts drawn as part of a crowd.
    constexpr u32 kCrowdShapes = (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 11);
    extern int g_FullLimit, g_DrawnLimit;
    extern u32 g_CrowdMask;
    extern float g_KeepMargin;
    extern u32 g_DetailChanges;
    extern bool g_Adapt;
    extern float g_Fps;
    // A level loads: the frame rate is not judged until it has settled.
    void SettleCrowd();
    // Each frame: the shown ghosts by distance from Link, nearest first, get their detail.
    void Rank(Game::Actors::Player* player, u32 frame, u64 now);

#ifdef LASAP_TEST
    // ---- DBG puppet (test_puppets.cpp) ----

    // Per frame of the replay: ghosts past their newest line (extrapolating, holding it); the traced slot's row.
    extern int g_Past, g_Held, g_TraceSlot;
    void TraceReplay(double now, double t, s64 at, u32 a, u32 count, char mode, float x, float z);
    void TestInstallHooks();
    // In TickPuppets: a level loads; a frame starts (Link's path recorded); the ghosts have been driven (the trace
    // row, DBG puppet's puppet moved); the frame ends (its log row).
    void TestLevelLoad();
    void TestFrameStart(Game::Actors::Player* player);
    void TestGhostsDriven(u32 frame, u64 now, double nowFine);
    void TestFrameEnd(u32 frame, u8 paused);
    // Each frame, the module's Tick's time (system ticks).
    void TestTickTime(u64 ticks);
#endif
}
