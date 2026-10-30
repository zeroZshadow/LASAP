// The DBG commands (test build, TESTING.md) on the game's state, and the STATE line.
#ifdef LASAP_TEST
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_popup.hpp"
#include "ap/ap_text.hpp"
#include "game/data/inventory.hpp"
#include "game/framework.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/hooks/inventory.hpp"
#include "game/hooks/level.hpp"
#include "game/map.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "game/ui.hpp"
#include "ghost/ghost_net.hpp"
#include "map/map_tracker.hpp"
#include "music/music.hpp"
#include "music/music_jukebox.hpp"
#include "test/test_impl.hpp"
#include "nn/fs.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace AP::Test {
namespace {
    using namespace AP::Bridge;
    using Game::NowMs;

    // DBG enemies: what the enemy randomizer did since the last DBG enemies reset
    std::atomic<int> g_EnemiesSeen = 0, g_EnemiesChanged = 0, g_EnemiesSized = 0;
    constexpr int kEnemyLog = 16;
    std::atomic<u32> g_EnemyLog[kEnemyLog];  // vanilla id << 16 | new id
    std::atomic<int> g_EnemyScale[kEnemyLog];  // scale x 100
    std::atomic<int> g_EnemyNext = 0;
    // DBG enemies kill: an actor whose actor table name starts so is an enemy; +0xa2 bit 2 marks it defeated, so its
    // destroy puts its key in the removal memory (ACTORS.md, Removal memory)
    constexpr const char* kEnemyPrefix = "Enemy";
    constexpr u16 kDefeated = 4;
    constexpr size_t kMaxKilled = 256;

    // DBG hit <actor table name>: a hit that counts on that enemy, as one its own checks let through (the side it is
    // hit from, its state, the weapon): the enemy's damage comp emits its damaged signal (result 0), which Hit_Dispatch
    // emits after the comp's damage lambda judged a hit so, and the enemy's own slot takes the damage, a phase or its
    // death (ACTORS.md, Enemy damage). Comps by vtable: the damage comp (its defence pointer at +0x278, its damaged
    // signal at +0x3b8, emitted through the signal's vtable +0x30 with (attacker record*, hit*)) and the reaction comp
    // (life at +0x168, dead: virtual +0xb8).
    constexpr uintptr_t kDamageCompVtable = 0x1b5b8b0, kReactionCompVtable = 0x1b5be00;
    constexpr size_t kCompDefence = 0x278, kCompDamaged = 0x3b8, kSignalEmit = 0x30, kReactionLife = 0x168;
    constexpr size_t kReactionDead = 0xb8;
    // The attacker is the attacking side's hit record (0x1da8): two weak pointers (null here), its hits from +0x20
    // (0x3b0 apart, up to 8) and their count at +0x1da0. The hit is one of them: the enemy's reaction copies the record
    // and finds the hit in it by the hit's two weak pointers, so the record holds the hit as its only one, and both
    // stay off the stack with the attack, which the reaction reads later. The hit: +0x0 the attacking side's attack
    // collision comp's weak pointer (Link's sword's: the Damage reaction reads its push vector at +0xf120, past an
    // actor's end), +0x10 the damage comp's, +0x20 the attack (+0x8 u16 power, +0xc not 0xb, +0xd the attack type),
    // +0x28 the defence (an f32 multiplier per attack type from +0x8), +0x30 where the hit effect plays, +0x70 the
    // direction (XZ). The power is the enemy's life: the hit ends its phase or its life.
    constexpr size_t kRecordSize = 0x1db0, kRecordHits = 0x20, kRecordCount = 0x1da0;
    constexpr size_t kHitAttacker = 0x0, kHitComp = 0x10, kHitAttack = 0x20, kHitDefence = 0x28;
    constexpr size_t kHitAt = 0x30, kHitDirection = 0x70;
    constexpr size_t kAttackSize = 0x40, kAttackPower = 0x8, kAttackType = 0xd;
    constexpr size_t kDefenceScales = 0x8, kAttackTypes = 20;
    constexpr const char* kSwordComp = "SwordAttackColComp";
    alignas(16) u8 g_Record[kRecordSize], g_Attack[kAttackSize];

    // Actors made with no LEB key (a boss's parts, as the Slime Eye's body and halves; ACTORS.md, Slime Eye), which the
    // registry does not hold: the last kSpawned made, weakly
    constexpr size_t kSpawned = 64;
    Game::WeakPtr g_Spawned[kSpawned]{};
    size_t g_SpawnedNext = 0;

    HOOK_DEFINE_TRAMPOLINE(SpawnTrack) {
        static void Callback(Game::SharedPtr* out, Game::SharedPtr* owner, Game::ActorParams* params) {
            Orig(out, owner, params);
            if (out->ptr == nullptr || params->lebKey != 0) return;
            Game::WeakPtr& slot = g_Spawned[g_SpawnedNext++ % kSpawned];
            Game::ReleaseWeakOwner(slot.ctrl);
            Game::AddWeakOwner(out->ctrl);
            slot = {out->ptr, out->ctrl};
        }
    };

    bool Named(const Game::Actor* actor, const char* prefix) {
        if (actor == nullptr || actor->entity.Gone() || actor->actorId >= Game::ActorTable_count) return false;
        const char* name = Game::ActorTable.Ptr()[actor->actorId].name;
        return name != nullptr && std::strncmp(name, prefix, std::strlen(prefix)) == 0;
    }

    // The registry's actors (the loaded rooms' LEB actors) and the ones made with no LEB key whose actor table name
    // starts with `prefix`, at most kMaxKilled. Returns how many.
    size_t FindActors(Game::Actor** found, const char* prefix) {
        Game::UnorderedMap* registry = *Game::ActorRegistry_ptr;
        if (registry == nullptr) return 0;
        struct RegistryNode { RegistryNode* next; u64 hash; u64 key; Game::SharedPtr actor; };
        size_t count = 0;
        for (auto* node = static_cast<RegistryNode*>(registry->first); node != nullptr && count < kMaxKilled;
             node = node->next) {
            auto* actor = static_cast<Game::Actor*>(node->actor.ptr);
            if (Named(actor, prefix)) found[count++] = actor;
        }
        for (const Game::WeakPtr& made : g_Spawned) {
            auto* actor = static_cast<Game::Actor*>(made.ptr);
            if (count < kMaxKilled && Game::Alive(made.ctrl) && Named(actor, prefix)) found[count++] = actor;
        }
        return count;
    }

    struct CompNode { CompNode* next; u64 hash; const char* name; u64 length; Game::SharedPtr comp; };

    // The actor's component whose vtable is the main module's `vtable` offset, or null
    u8* ComponentOf(Game::Actor* actor, uintptr_t vtable) {
        uintptr_t want = Game::Addr(vtable);
        for (auto* node = static_cast<CompNode*>(actor->components.first); node != nullptr; node = node->next) {
            auto* comp = static_cast<u8*>(node->comp.ptr);
            if (comp != nullptr && *reinterpret_cast<uintptr_t*>(comp) == want) return comp;
        }
        return nullptr;
    }

    // The actor's component of that name, or null
    u8* ComponentNamed(Game::Actor* actor, const char* name) {
        size_t length = std::strlen(name);
        for (auto* node = static_cast<CompNode*>(actor->components.first); node != nullptr; node = node->next) {
            if (node->length == length && std::memcmp(node->name, name, length) == 0) {
                return static_cast<u8*>(node->comp.ptr);
            }
        }
        return nullptr;
    }

    // DBG comps: " <name>:<vtable offset>" per component of each actor of that actor table name in the loaded rooms,
    // actors apart by " |"
    std::string ComponentList(const std::string& name) {
        Game::Actor* actors[kMaxKilled];
        size_t count = FindActors(actors, name.c_str());
        std::string s;
        char buf[0x30];
        for (size_t i = 0; i < count; i++) {
            if (name != Game::ActorTable.Ptr()[actors[i]->actorId].name) continue;
            if (!s.empty()) s += " |";
            for (auto* node = static_cast<CompNode*>(actors[i]->components.first); node != nullptr; node = node->next) {
                auto* comp = static_cast<u8*>(node->comp.ptr);
                uintptr_t vtable = comp != nullptr ? *reinterpret_cast<uintptr_t*>(comp) - Game::Addr(0) : 0;
                std::snprintf(buf, sizeof(buf), ":%lx", static_cast<unsigned long>(vtable));
                s += " " + std::string(node->name, node->length) + buf;
            }
        }
        return s;
    }

    // The Slime Eye (ACTORS.md, Slime Eye): its controller (EnemyDeguZol, a LEB actor) and its body
    // (EnemyDeguZolPhase1, made with no LEB key, a shared pointer at the controller's +0x1328), each with its state
    // machine at +0xff0 and the state at +0xffd. The controller falls from Sleep (0) to its start event (1), then waits
    // (2) and rains Zols (3) until Link's boots bonk sets Phase1 (4); the body then drops from the ceiling (its states
    // 4, 7, 8 PopFreeFall, 9, 10 Wait). A body hit at stage 4 (+0x1980, SetStage) in Damage (12) splits it into its
    // halves (EnemyDeguZolPhase2), as a boots dash into it does.
    constexpr size_t kSlimeMachine = 0xff0, kSlimeState = 0xffd, kSlimeBody = 0x1328;
    constexpr u8 kSlimeWait = 2, kSlimeRain = 3, kSlimePhase1 = 4, kBodyDamage = 12;
    constexpr s32 kSplitStage = 4;
    constexpr Game::Func<void(u8*, s32)> DeguZol_ChangeState{0x2bc960};
    constexpr Game::Func<void(u8*, s32)> DeguZolPhase1_ChangeState{0x2d7140};
    constexpr Game::Func<void(u8*, s32)> DeguZolPhase1_SetStage{0x2ea810};

    // DBG slimeeye: " state=<controller's> body=<body's, - without one>"; with fall, first the bonk's change to
    // Phase1 (from the wait or the rain only), with split the split; an ERR reply's reason otherwise
    std::string SlimeEye(const std::string& what, bool* ok) {
        *ok = false;
        Game::Actor* found[kMaxKilled];
        size_t count = FindActors(found, "EnemyDeguZol");
        u8* controller = nullptr;
        for (size_t i = 0; i < count; i++) {
            if (std::strcmp(Game::ActorTable.Ptr()[found[i]->actorId].name, "EnemyDeguZol") == 0) {
                controller = reinterpret_cast<u8*>(found[i]);
            }
        }
        if (controller == nullptr) return "no controller";
        auto* body = reinterpret_cast<Game::SharedPtr*>(controller + kSlimeBody);
        bool hasBody = body->ptr != nullptr && Game::Alive(body->ctrl);
        auto* bodyAt = static_cast<u8*>(body->ptr);
        if (what == "fall") {
            u8 state = controller[kSlimeState];
            if (state != kSlimeWait && state != kSlimeRain) return "state " + std::to_string(state);
            DeguZol_ChangeState(controller + kSlimeMachine, kSlimePhase1);
        } else if (what == "split") {
            if (!hasBody) return "no body";
            DeguZolPhase1_SetStage(bodyAt, kSplitStage);
            DeguZolPhase1_ChangeState(bodyAt + kSlimeMachine, kBodyDamage);
        } else if (!what.empty()) {
            return "unknown " + what;
        }
        *ok = true;
        return " state=" + std::to_string(controller[kSlimeState]) +
               " body=" + (hasBody ? std::to_string(bodyAt[kSlimeState]) : std::string("-"));
    }

    // DBG hit: " life=<before> type=<attack type> part=<damage comp>" after the hit on the first actor of that name in
    // the loaded rooms, on its damage comp named `wanted` (any when empty); an ERR reply's reason otherwise
    std::string HitEnemy(const std::string& name, const std::string& wanted, bool* ok) {
        *ok = false;
        Game::Actors::Player* player = Game::GetPlayer();
        if (player == nullptr) return "no player";
        u8* sword = ComponentNamed(&player->actor, kSwordComp);
        if (sword == nullptr) return "no sword";
        Game::Actor* actors[kMaxKilled];
        size_t count = FindActors(actors, name.c_str());
        // none while Link is in an event (state 22): no hit lands during one, and one during a boss's dead event starts
        // its death again
        if (Holds("event=22")) return "event";
        // the first of that name with a damage comp, one with life left before one without (Master Stalfos's falls
        // apart at none, and a boss may have more than one actor)
        Game::Actor* actor = nullptr;
        u8* comp = nullptr;
        u8* reaction = nullptr;
        s32 life = 0;
        for (size_t i = 0; i < count; i++) {
            if (name != Game::ActorTable.Ptr()[actors[i]->actorId].name) continue;
            if (ComponentOf(actors[i], kDamageCompVtable) == nullptr) continue;
            u8* its = ComponentOf(actors[i], kReactionCompVtable);
            s32 left = its != nullptr ? *reinterpret_cast<s32*>(its + kReactionLife) : 0;
            if (actor != nullptr && (left < 1 || life > 0)) continue;
            actor = actors[i], reaction = its, life = left;
        }
        if (actor == nullptr) return "no " + name;
        // none after the killing blow, before the dead event starts: a boss's own handler takes the death branch again
        if (reaction != nullptr && Game::CallVirtual<bool()>(reaction, kReactionDead)) return "dead";
        // of its damage comps (a part each, as Moldorm's head, body and tail), the one and the attack type its defence
        // scales most: the weak spot
        u8* defence = nullptr;
        u8 type = 0;
        float best = -1;
        std::string part;
        for (auto* node = static_cast<CompNode*>(actor->components.first); node != nullptr; node = node->next) {
            auto* damage = static_cast<u8*>(node->comp.ptr);
            if (damage == nullptr || *reinterpret_cast<uintptr_t*>(damage) != Game::Addr(kDamageCompVtable)) continue;
            if (!wanted.empty() && wanted != std::string_view(node->name, node->length)) continue;
            u8* its = *reinterpret_cast<u8**>(damage + kCompDefence);
            if (its == nullptr) continue;
            auto* scales = reinterpret_cast<const float*>(its + kDefenceScales);
            for (u8 t = 0; t < kAttackTypes; t++) {
                if (scales[t] <= best) continue;
                comp = damage, defence = its, type = t, best = scales[t];
                part.assign(node->name, node->length);
            }
        }
        if (defence == nullptr) return "no defence";
        std::memset(g_Record, 0, sizeof(g_Record));
        std::memset(g_Attack, 0, sizeof(g_Attack));
        *reinterpret_cast<u16*>(g_Attack + kAttackPower) = u16(std::clamp(life, 1, 0xffff));
        g_Attack[kAttackType] = type;
        u8* hit = g_Record + kRecordHits;
        *reinterpret_cast<u32*>(g_Record + kRecordCount) = 1;
        *reinterpret_cast<Game::WeakPtr*>(hit + kHitAttacker) = reinterpret_cast<Game::Entity*>(sword)->self;
        *reinterpret_cast<Game::WeakPtr*>(hit + kHitComp) = reinterpret_cast<Game::Entity*>(comp)->self;
        *reinterpret_cast<u8**>(hit + kHitAttack) = g_Attack;
        *reinterpret_cast<u8**>(hit + kHitDefence) = defence;
        if (actor->root != nullptr) {
            const Game::Vec3& at = actor->root->coords.pos;
            *reinterpret_cast<Game::Vec4*>(hit + kHitAt) = {at.x, at.y, at.z, 0};
        }
        *reinterpret_cast<Game::Vec4*>(hit + kHitDirection) = {0, 0, 1, 0};
        u8* damaged = comp + kCompDamaged;
        Game::CallVirtual<void(void*, u8*)>(damaged, kSignalEmit, g_Record, hit);
        *ok = true;
        return " life=" + std::to_string(life) + " type=" + std::to_string(type) + " part=" + part;
    }

    // DBG enemies at [prefix]: each actor's of the loaded rooms whose actor table name starts so (default: the
    // enemies) actor table name and world position
    std::string ActorPlaces(const char* prefix) {
        Game::Actor* enemies[kMaxKilled];
        size_t count = FindActors(enemies, prefix);
        std::string s;
        char buf[96];
        for (size_t i = 0; i < count; i++) {
            if (enemies[i]->root == nullptr) continue;
            const Game::Vec3& at = enemies[i]->root->coords.pos;
            snprintf(buf, sizeof(buf), " %s@%.2f,%.2f,%.2f", Game::ActorTable.Ptr()[enemies[i]->actorId].name, at.x,
                     at.y, at.z);
            s += buf;
        }
        return s;
    }

    // The enemies, defeated: each removed from its room as the game removes an actor (Entity_RemoveChild, destroyed
    // later), with no death of its own (no drop, no event). LEB ones stay away while their zone is among the last five
    // visited. Returns how many.
    int KillEnemies() {
        Game::Actor* enemies[kMaxKilled];
        size_t count = FindActors(enemies, kEnemyPrefix);
        int killed = 0;
        for (size_t i = 0; i < count; i++) {  // after the walk: a removal may change the registry
            Game::Actor* actor = enemies[i];
            if (!Game::Alive(actor->entity.parent.ctrl) || actor->entity.parent.ptr == nullptr) continue;
            if (actor->lebKey != 0) actor->zoneState |= kDefeated;
            if (Game::Entity_RemoveChild(actor->entity.parent.ptr, actor->entity.name, actor->entity.nameLength, false)) {
                killed++;
            }
        }
        return killed;
    }

    // Each dungeon's entry by LevelIndex (0-7 D1-D8, 8 the Chamber Dungeon, 9 the Color Dungeon) as
    // <small keys>.<item bits>, and the current LevelIndex (255 outside dungeons)
    std::string DungeonItemsField() {
        Game::Map::DungeonItemsEntry** entries = *Game::DungeonItems_ptr;
        if (entries == nullptr) return "";
        std::string s = " dkeys=";
        for (int i = 0; i < 10; i++) {
            if (i > 0) s += ",";
            if (entries[i] == nullptr) s += "-";
            else s += std::to_string(entries[i]->smallKeys) + "." + std::to_string(entries[i]->flags);
        }
        return s + " dlevel=" + std::to_string(*Game::LevelIndex);
    }

    std::string StateLine() {
        Game::Framework* fmwk = Game::GetFramework();
        auto* items = Game::Data::Inventory::Items();
        std::string s = "STATE level=" + LevelSystem::GetCurrentLevel();
        s += " screen=" + ScreenName(fmwk);
        s += " ms_in_level=" + std::to_string(NowMs() - g_LevelSince);
        s += " player=" + std::string(fmwk && fmwk->player ? "1" : "0");
        if (items != nullptr) {
            s += " health=" + std::to_string(items->Health);
            s += " rupees=" + std::to_string(items->Rupees);
            s += " shells=" + Hex(items->Seashells);
            s += " hp=" + Hex(items->HeartPieces);
            s += " hc=" + std::to_string(items->HeartContainers);
            s += " leaves=" + std::to_string(items->GoldenLeaves);
            s += " stones=" + Hex(ChamberStoneBits());
            s += " medicine=" + std::to_string(items->ExtraItems.Medicine) + "/" + std::to_string(GetMedicineWaiting());
            s += " sword=" + std::to_string(items->MainItems.SwordLv1) + "/" + std::to_string(items->UpgradeItems.SwordLv2);
            s += " shield=" + std::to_string(items->MainItems.Shield) + "/" + std::to_string(items->UpgradeItems.MirrorShield);
            s += " bracelet=" + std::to_string(items->MainItems.PowerBraceletLv1) + "/" + std::to_string(items->UpgradeItems.PowerBraceletLv2);
            s += " feather=" + std::to_string(items->MainItems.RocsFeather);
            s += " boots=" + std::to_string(items->MainItems.PegasusBoots);
            s += " flippers=" + std::to_string(items->ExtraItems.Flippers);
            s += " bombs=" + std::to_string(items->BombCount);
            s += " arrows=" + std::to_string(items->ArrowCount);
            s += " bombbag=" + std::to_string(items->MainItems.Bomb);
            s += " bow=" + std::to_string(items->MainItems.Bow);
            s += " cello=" + std::to_string(items->ExtraItems.FullMoonCello);
            s += DungeonItemsField();
        }
        if (fmwk && fmwk->player && fmwk->player->playerCollision) {
            auto& p = fmwk->player->playerCollision->base.node.coordsNew.pos;
            char buf[96];
            snprintf(buf, sizeof(buf), " pos=%.2f,%.2f,%.2f", p.x, p.y, p.z);
            s += buf;
        }
        s += " recv=" + std::to_string(GetRecvCount());
        if (fmwk && fmwk->player) {
            s += " event=" + std::to_string(fmwk->player->machine.state);
            s += " paused=" + std::to_string(fmwk->paused);
            s += std::string(" carry=") + (PlayerCarrying() ? "1" : "0");
        }
        s += ItemGetState();
        s += std::string(" popup=") + (AP::Popup::Busy() ? "1" : "0");
        s += std::string(" toast=") + (AP::ToastShowing() ? "1" : "0");
        int ghosts = 0;
        for (int n = 0; n < AP::GhostNet::SLOTS; n++) ghosts += AP::GhostNet::Get(n).active;
        s += " ghosts=" + std::to_string(ghosts);
        s += AP::Music::StateField();
        s += AP::JukeboxPane::StateField(fmwk != nullptr && fmwk->paused != 0);
        s += MapTracker::ListField();
        s += " puppets=" + Puppets::StateField() + TraceStateFields();
        return s;
    }

    // The commands on the game's state. Returns false if the command must wait for the player.
    bool GameCommand(const std::vector<std::string>& a) {
        const std::string& cmd = a[1];
        if (cmd == "state") {
            AP::Send(StateLine());
        } else if (cmd == "flag" && a.size() >= 3) {
            AP::Send("FLAG " + a[2] + " " + (EventFlags::CheckFlag(a[2]) ? "1" : "0"));
        } else if (cmd == "setflag" && a.size() >= 4) {
            EventFlags::SetFlag(a[2], a[3] == "1");
            AP::Send("FLAG " + a[2] + " " + (EventFlags::CheckFlag(a[2]) ? "1" : "0"));
        } else if (cmd == "give" && a.size() >= 5) {
            // Raw AddItemID through the LASR hook: DBG give <id> <count> <index>
            if (!PlayerReady()) return false;
            Inventory::AddItemID(atoi(a[2].c_str()), atoi(a[3].c_str()), atoi(a[4].c_str()));
            AP::Send("OK give");
        } else if (cmd == "grant" && a.size() >= 5) {
            // Same path as ITEM without touching the received counter: DBG grant <key> <count> <dungeon> [show 0|1]
            if (!PlayerReady()) return false;
            int id = GrantItem(a[2], atoi(a[3].c_str()), atoi(a[4].c_str()));
            if (IsTrapKey(a[2])) QueueTrap(a[2], "", "", -1);
            else if (id >= 0 && (a.size() < 6 || a[5] != "0")) QueueShow(Show{id, "", "", ""});
            AP::Send("OK grant");
        } else if (cmd == "heldangle" && a.size() >= 6) {
            // An NPC model's item-get angle until the game restarts: DBG heldangle <model> <pitch> <yaw> <roll>
            Game::Vec3 angle{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
            if (SetHeldAngle(a[2], angle)) AP::Send("OK heldangle");
            else AP::Send("ERR heldangle unknown model " + a[2]);
        } else if (cmd == "popsway" && a.size() >= 3) {
            // DBG popsway <on|off>: whether the popup's model turns from side to side, until the game restarts
            AP::Popup::Sway(a[2] != "off");
            AP::Send("OK popsway");
        } else if (cmd == "popdraw" && a.size() >= 3) {
            // DBG popdraw stats: the popup's opaque items held per pass id since the last call;
            // DBG popdraw <on|off>: whether the model draws in front of the world, until the game restarts
            if (a[2] == "stats") {
                AP::Send("OK popdraw" + AP::Popup::DrawStats());
            } else {
                AP::Popup::SetOnTop(a[2] != "off");
                AP::Send("OK popdraw");
            }
        } else if (cmd == "popframes" && a.size() >= 3) {
            // DBG popframes <count>: record the popup's next poses; DBG popframes dump: send them
            if (a[2] == "dump") {
                for (const auto& line : AP::Popup::RecordedFrames()) AP::Send(line);
                AP::Send("OK popframes");
            } else {
                AP::Popup::RecordFrames(static_cast<size_t>(atoi(a[2].c_str())));
                AP::Send("OK popframes");
            }
        } else if (cmd == "heldpose") {
            // DBG heldpose: the world rotations (quaternions) of Link's model, the held NPC model's parent node and
            // the model itself during an item-get, and the model's local rotation
            auto quat = [](const Game::Vec4& r) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.3f", r.x, r.y, r.z, r.t);
                return std::string(buf);
            };
            Game::Actors::Player* player = Game::GetPlayer();
            Game::Node* link = player != nullptr && player->modelComp != nullptr ? &player->modelComp->node : nullptr;
            Game::Node* held = HeldModel();
            std::string out = "HELDPOSE link=" + (link ? quat(link->coords.rotate) : std::string("-"));
            if (held != nullptr) {
                out += " parent=" + (held->parent ? quat(held->parent->coords.rotate) : std::string("-"));
                out += " model=" + quat(held->coords.rotate) + " local=" + quat(held->coordsNew.rotate);
            }
            // any item-get model's local pose (the game's pose by item id) and the popup's scale
            Game::Node* item = ItemGetModel();
            if (item != nullptr) {
                const Game::Coords& c = item->coordsNew;
                char buf[160];
                snprintf(buf, sizeof(buf), " item=%.3f at=%.3f,%.3f,%.3f rot=%s", c.scale.y, c.pos.x, c.pos.y, c.pos.z,
                         quat(c.rotate).c_str());
                out += buf;
            }
            char popup[32];
            snprintf(popup, sizeof(popup), " popup=%.3f", AP::Popup::Scale());
            out += popup;
            // the camera's view rotation
            const Game::Camera* camera = Game::GetCamera();
            if (camera != nullptr) {
                out += " view=";
                for (int i = 0; i < 12; i++) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), i ? ",%.3f" : "%.3f", camera->view[i]);
                    out += buf;
                }
            }
            AP::Send(out);
        } else if (cmd == "warp" && a.size() >= 6) {
            // DBG warp <level> <x> <y> <z>
            if (!PlayerReady()) return false;
            LevelSystem::LoadLevel(a[2].c_str(), Game::Vec3{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())});
            AP::Send("OK warp");
        } else if (cmd == "pos" && a.size() >= 5) {
            // Moves the player within the current level: DBG pos <x> <y> <z>
            if (!PlayerReady()) return false;
            // Setting the respawn point too means a fall into a room that is not loaded yet lands at the target.
            // Node_MarkDirty emits the node's transformChanged (only while needsCoordUpdate is clear), whose listener
            // puts the character controller there too: Link's Move moves from the controller's position, which a
            // swimming Link's state never puts back on the node (PLAYER.md, Swim).
            auto* player = Game::GetFramework()->player;
            auto* col = player->playerCollision;
            Game::Vec3 target{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
            col->base.node.coordsNew.pos = target;
            col->base.node.coords.pos = target;
            col->base.node.needsCoordUpdate = false;
            Game::Node_MarkDirty(&col->base.node);
            player->respawnCoords.pos = target;
            AP::Send("OK pos");
        } else if (cmd == "setrecv" && a.size() >= 3) {
            SetRecvCount(atoi(a[2].c_str()));
            AP::Send("RECV " + std::to_string(GetRecvCount()));
        } else if (cmd == "clearchecks") {
            ClearChecks();
            AP::Send("OK clearchecks");
        } else if (cmd == "map") {
            if (a.size() >= 3) MapTracker::SetMode(atoi(a[2].c_str()));
            AP::Send(MapTracker::State());
        } else if (cmd == "maplist") {
            AP::Send(MapTracker::ListInfo(a));
        } else if (cmd == "mapdot") {
            AP::Send(MapTracker::DotInfo(a));
        } else if (cmd == "heap") {
            // DBG heap: the module's heap (exl::setting::HeapSize): bytes taken from it, of those in use and free
            struct mallinfo mi = mallinfo();
            AP::Send("OK heap arena=" + std::to_string(mi.arena) + " used=" + std::to_string(mi.uordblks) +
                     " free=" + std::to_string(mi.fordblks) + " size=" + std::to_string(exl::setting::HeapSize));
        } else if (cmd == "maplabel") {
            AP::Send("MAPLABEL " + MapTracker::LabelText(a.size() >= 3 ? atoi(a[2].c_str()) : MapTracker::HoverGroup()));
        } else if (cmd == "text") {
            AP::Send(a.size() >= 3 ? AP::SeenText(a[2]) : AP::TextState());
        } else if (cmd == "music") {
            // DBG music: the jukebox's STATE fields and the last BGM requests; DBG music <track>|off: chooses as the
            // Jukebox page does
            if (a.size() >= 3) AP::Music::Choose(a[2]);
            AP::Send("MUSIC" + AP::Music::StateField() + " " + AP::Music::LogText());
        } else if (cmd == "jukebox") {
            // DBG jukebox: the Jukebox page's state
            AP::Send(AP::JukeboxPane::Info());
        } else if (cmd == "name") {
            // DBG name: the player's name as the game holds it for the save (UTF-16, up to 8 letters, at +0x8 of the
            // save header), set by a new file's name entry on OK
            const Game::Data::SaveHeader* info = *Game::SaveHeader_ptr;
            if (info == nullptr) {
                AP::Send("ERR name: no object");
                return true;
            }
            const char16_t* name = info->name;
            std::string utf8;
            for (int i = 0; i < 8 && name[i] != 0; i++) {
                char16_t c = name[i];
                if (c < 0x80) {
                    utf8 += static_cast<char>(c);
                } else if (c < 0x800) {
                    utf8 += static_cast<char>(0xc0 | (c >> 6));
                    utf8 += static_cast<char>(0x80 | (c & 0x3f));
                } else {
                    utf8 += static_cast<char>(0xe0 | (c >> 12));
                    utf8 += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                    utf8 += static_cast<char>(0x80 | (c & 0x3f));
                }
            }
            AP::Send("PLAYERNAME " + utf8);
        } else if (cmd == "uis") {
            // DBG uis: the UI manager's UIs (id, `a` while active, vtable) and the game state machine's scene's vtable
            std::string s = "UIS";
            Game::Ui::Manager& manager = *Game::UiManager;
            int n = 0;
            for (Game::ListNode* node = manager.uis.end.next; node != nullptr && node != &manager.uis.end && n < 48;
                 node = node->next, n++) {
                auto* ui = *reinterpret_cast<Game::Ui::LayoutWrapper**>(node + 1);
                if (ui == nullptr) continue;
                int id = Game::CallVirtual<int()>(ui, Game::Ui::LayoutWrapperVirtual::kUiId);
                uintptr_t vtable = reinterpret_cast<uintptr_t>(ui->vtable) - Game::Base();
                s += " " + Hex(id) + (ui->active ? "a" : "") + "@" + Hex(vtable);
            }
            Game::Framework* fmwk = Game::GetFramework();
            const Game::GameState* state = fmwk != nullptr ? fmwk->gameState : nullptr;
            const uintptr_t* scene = state != nullptr ? static_cast<const uintptr_t*>(state->scene) : nullptr;
            s += " | scene=" + (scene != nullptr ? Hex(*scene - Game::Base()) : std::string("-"));
            s += " nosave=" + std::to_string(NoSaveLoaded(fmwk));
            AP::Send(s);
        } else if (cmd == "peek" && a.size() >= 5) {
            // DBG peek <p|f|i|l|b|m|h> <hex offset> <hex length>: bytes of the player, the framework, the inventory, the
            // level flags, the BGM manager, its first playing instance or that instance's sound
            Game::Framework* fmwk = Game::GetFramework();
            const u8* base = nullptr;
            if (a[2] == "p") base = fmwk ? reinterpret_cast<const u8*>(fmwk->player) : nullptr;
            else if (a[2] == "l") base = *Game::LevelFlags_bits.Ptr();
            else if (a[2] == "f") base = reinterpret_cast<const u8*>(fmwk);
            else if (a[2] == "i") base = reinterpret_cast<const u8*>(Game::Data::Inventory::Items());
            else if (a[2] == "b" || a[2] == "m") base = AP::Music::PeekBase(a[2] == "m");
            else if (a[2] == "h" && AP::Music::PeekBase(true) != nullptr) {
                base = *reinterpret_cast<const u8* const*>(AP::Music::PeekBase(true) + 8);  // the sound
            }
            if (base == nullptr) {
                AP::Send("ERR peek: no object");
                return true;
            }
            size_t off = strtoull(a[3].c_str(), nullptr, 16), len = std::min<size_t>(strtoull(a[4].c_str(), nullptr, 16), 0x2000);
            std::string line = "PEEK " + a[2] + " " + a[3] + " ";
            char hex[3];
            for (size_t i = 0; i < len; i++) {
                snprintf(hex, sizeof(hex), "%02x", base[off + i]);
                line += hex;
                if (line.size() > 1800) {
                    AP::Send(line);
                    line = "PEEK+ ";
                }
            }
            AP::Send(line);
        } else if (cmd == "poke" && a.size() >= 4) {
            // DBG poke <hex offset> <hex bytes>: writes bytes into the inventory
            u8* base = reinterpret_cast<u8*>(Game::Data::Inventory::Items());
            size_t off = strtoull(a[2].c_str(), nullptr, 16);
            const std::string& hex = a[3];
            if (base == nullptr || hex.size() % 2 != 0 || off + hex.size() / 2 > sizeof(Game::Data::Inventory::InventoryItems)) {
                AP::Send("ERR poke");
                return true;
            }
            for (size_t i = 0; i < hex.size() / 2; i++) {
                base[off + i] = static_cast<u8>(strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16));
            }
            AP::Send("OK poke");
        } else if (cmd == "itemget" && a.size() >= 3) {
            // DBG itemget <item key or id> [message entry]: the item event, without granting or waiting in the queue.
            // Only for looking at item models: it skips the checks that hold received items back, so it can break a
            // running flow. Tests of received items use grant or ITEM lines.
            if (!PlayerReady()) return false;
            if (!ItemEventIdle()) {
                AP::Send("ERR itemget: a display is running");
                return true;
            }
            bool numeric = !a[2].empty() && a[2].find_first_not_of("0123456789") == std::string::npos;
            int id = numeric ? std::stoi(a[2]) : ItemIdFromKey(a[2]);
            bool ok = id >= 0 && TestItemEvent(id, a.size() >= 4 ? a[3] : "");
            AP::Send(std::string(ok ? "OK" : "ERR") + " itemget " + std::to_string(id));
        } else if (cmd == "pickup" && a.size() >= 3) {
            // DBG pickup <item key or id>: what an item-get of it does with Instant Pickups, without the event
            if (!PlayerReady()) return false;
            bool numeric = a[2].find_first_not_of("0123456789") == std::string::npos;
            int id = numeric ? std::stoi(a[2]) : ItemIdFromKey(a[2]);
            if (id >= 0) InstantPickup(id);
            AP::Send(std::string(id >= 0 ? "OK" : "ERR") + " pickup " + std::to_string(id));
        } else if (cmd == "popmodel" && a.size() >= 3) {
            // DBG popmodel <item key or id>: the received-item popup's model alone, without the toast or the queue
            if (!PlayerReady()) return false;
            bool numeric = a[2].find_first_not_of("0123456789") == std::string::npos;
            int id = numeric ? std::stoi(a[2]) : ItemIdFromKey(a[2]);
            bool ok = id >= 0 && AP::Popup::Show(id);
            AP::Send(std::string(ok ? "OK" : "ERR") + " popmodel " + std::to_string(id));
        } else if (cmd == "dumpmain") {
            // Writes the loaded, decrypted main module (text..data) to sd:/lasap/main.bin for reverse engineering.
            const auto& info = exl::util::GetMainModuleInfo();
            uintptr_t start = info.m_Total.m_Start;
            size_t size = info.m_Data.m_Start + info.m_Data.m_Size - start;
            nn::fs::CreateDirectory("sd:/lasap");
            nn::fs::DeleteFile("sd:/lasap/main.bin");
            nn::fs::CreateFile("sd:/lasap/main.bin", size);
            nn::fs::FileHandle h{};
            if (R_FAILED(nn::fs::OpenFile(&h, "sd:/lasap/main.bin", nn::fs::OpenMode_Write))) {
                AP::Send("ERR cannot open main.bin");
                return true;
            }
            nn::fs::WriteFile(h, 0, reinterpret_cast<void*>(start), size, nn::fs::WriteOption{nn::fs::WriteOptionFlag_Flush});
            nn::fs::CloseFile(h);
            AP::Send("OK dumpmain base=" + Hex(start) + " size=" + Hex(size) + " text=" + Hex(info.m_Text.m_Size) +
                     " rodata=" + Hex(info.m_Rodata.m_Start - start) + " data=" + Hex(info.m_Data.m_Start - start));
        } else if (cmd == "savepopup") {
            // Shows the game's own autosave popup, to check that toasts give its text back
            Game::ThrobberSave_Show(true);
            AP::Send("OK savepopup");
        } else if (cmd == "collect" && a.size() >= 3) {
            // Simulates picking up a placeholder: DBG collect <local_id>
            AP::OnPlaceholderCollected(atoi(a[2].c_str()));
        } else if (cmd == "enemies") {
            // DBG enemies [reset]: enemies seen, changed and resized, and the last few as <vanilla id>><id>@<scale%>;
            // DBG enemies kill: removes every enemy of the loaded rooms (KillEnemies); DBG enemies at: where they are
            if (a.size() >= 3 && a[2] == "kill") {
                AP::Send("OK enemies killed=" + std::to_string(KillEnemies()));
                return true;
            }
            if (a.size() >= 3 && a[2] == "at") {
                AP::Send("OK enemies at" + ActorPlaces(a.size() >= 4 ? a[3].c_str() : kEnemyPrefix));
                return true;
            }
            if (a.size() >= 3 && a[2] == "reset") {
                g_EnemiesSeen = g_EnemiesChanged = g_EnemiesSized = g_EnemyNext = 0;
            }
            std::string s = "OK enemies seen=" + std::to_string(g_EnemiesSeen.load()) +
                            " changed=" + std::to_string(g_EnemiesChanged.load()) +
                            " sized=" + std::to_string(g_EnemiesSized.load()) + " last=";
            int next = g_EnemyNext.load();
            for (int n = std::max(0, next - kEnemyLog); n < next; n++) {
                u32 e = g_EnemyLog[n % kEnemyLog];
                s += Hex(e >> 16) + ">" + Hex(e & 0xffff) + "@" + std::to_string(g_EnemyScale[n % kEnemyLog].load()) +
                     (n + 1 < next ? "," : "");
            }
            AP::Send(s);
        } else if (cmd == "hit" && a.size() >= 3) {
            // DBG hit <actor table name> [damage comp]: a hit that counts on that enemy, on that part (HitEnemy)
            bool ok;
            std::string said = HitEnemy(a[2], a.size() >= 4 ? a[3] : "", &ok);
            AP::Send(ok ? "OK hit " + a[2] + said : "ERR hit " + said);
        } else if (cmd == "comps" && a.size() >= 3) {
            // DBG comps <actor table name>: its components' names and vtables (ComponentList)
            AP::Send("OK comps" + ComponentList(a[2]));
        } else if (cmd == "slimeeye") {
            // DBG slimeeye [fall|split]: the Slime Eye's states, after making it fall or split (SlimeEye)
            bool ok;
            std::string said = SlimeEye(a.size() >= 3 ? a[2] : "", &ok);
            AP::Send(ok ? "OK slimeeye" + said : "ERR slimeeye " + said);
        } else if (cmd == "zols") {
            // DBG zols: where the last Zol Trap let out its enemies, the traps so far and its enemies still alive
            AP::Send("OK zols" + ZolTrapState());
        } else if (cmd == "drop") {
            // DBG drop: where the drop actor made last is (ap_drop.cpp)
            AP::Send("OK drop" + DropState());
        } else if (cmd == "ghostnet") {
            // DBG ghostnet: the positions sent and the ghosts received (ghost_net.cpp)
            AP::Send("OK " + AP::GhostNet::State());
        } else if (cmd == "flood" && a.size() >= 4) {
            // DBG flood <lines> <bytes>: LOG lines of that many bytes for the AP client, to fill the socket of a
            // client that reads nothing (e2e op stall)
            std::string line = "LOG flood " + std::string(strtoul(a[3].c_str(), nullptr, 10), 'x');
            for (int i = atoi(a[2].c_str()); i > 0; i--) AP::Send(line);
            AP::Send("OK flood");
        } else {
            AP::Send("ERR unknown debug command");
        }
        return true;
    }
}

    void OnEnemy(u16 vanilla, u16 now, float scale) {
        g_EnemiesSeen++;
        if (now != vanilla) g_EnemiesChanged++;
        if (scale != 0.0f && scale != 1.0f) g_EnemiesSized++;
        int i = g_EnemyNext++ % kEnemyLog;
        g_EnemyLog[i] = (u32(vanilla) << 16) | now;
        g_EnemyScale[i] = int(scale * 100.0f);
    }

    bool Command(const std::vector<std::string>& a) {
        if (a.size() < 2) return true;
        if (InputCommand(a) || TraceCommand(a) || Puppets::Command(a)) return true;
        return GameCommand(a);
    }

    void InstallSpawnTrack() { SpawnTrack::InstallAtFuncPtr(Game::Actor_Create.Ptr()); }
}
#endif
