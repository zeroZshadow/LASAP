// The Zol Trap (DESIGN.md, Showing received items): the game's chests of $ENEMY let out a Zol. Here every chest at a
// location holds the placeholder, so the trap comes back as a received item: the chest that held it lets out its Zol,
// else Zols appear beside Link.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "game/actor.hpp"
#include "game/framework.hpp"
#include "game/sym.hpp"
#include "lib.hpp"
#include <cstring>

namespace AP::Bridge {
namespace {
    using Game::Shared;
    using Game::Weak;

    constexpr u16 kTreasureBox = 0xf7, kZolGreen = 0x1b;
    // A chest's find: its placeholder's AddItemID in the chest's event, Link standing in front of the chest.
    constexpr float kChestReach = 2.5f;
    // Beside Link: one tile to his left and right, also in a side view.
    constexpr float kBesideLink[] = {-1.5f, 1.5f};
    constexpr size_t kKept = sizeof(kBesideLink) / sizeof(kBesideLink[0]);
    constexpr float kAboveFloor = 0.7f;  // as high as the chest lets out its Zol above its root
    // The chest gives its Zol the LEB parameter 0 int 3 (ObjTreasureBox_PopEnemy): it jumps out and lands. On the
    // overworld such a Zol falls through the ground, so there the chest's Zol is made without it, as Link's are.
    constexpr const char* kOverworld = "Field";

    Weak<Game::Actor> g_FindChest;  // the chest of the last chest find, and its location
    std::optional<AP::LocationId> g_FindLocation;
#ifdef LASAP_TEST
    const char* g_From = "-";
    int g_Spawned = 0;
    Weak<Game::Actor> g_Zols[kKept];  // the last trap's enemies
#endif

    // The actor while it is there and not gone (game thread)
    Game::Actor* Live(const Weak<Game::Actor>& weak) {
        Game::Actor* actor = weak.Peek();
        return actor != nullptr && !actor->entity.Gone() ? actor : nullptr;
    }

#ifdef LASAP_TEST
    void KeepSpawned(size_t i, const Shared<Game::Actor>& enemy) {
        if (i < kKept) g_Zols[i] = enemy;
    }
#endif

    // The chest nearest Link within reach in his zone, among the actors of the registry (LEB actors), or null.
    Game::Actor* ChestBesideLink(const Game::Actor& link) {
        Game::UnorderedMap* registry = *Game::ActorRegistry_ptr;
        if (registry == nullptr || link.root == nullptr) return nullptr;
        const Game::Vec3& at = link.root->coords.pos;
        Game::Actor* nearest = nullptr;
        float best = kChestReach * kChestReach;
        for (auto* node = static_cast<Game::ActorRegistryNode*>(registry->first); node != nullptr; node = node->next) {
            Game::Actor* actor = node->actor.get();
            if (actor == nullptr || actor->actorId != kTreasureBox || actor->zone != link.zone ||
                actor->root == nullptr || actor->entity.Gone()) {
                continue;
            }
            const Game::Vec3& p = actor->root->coords.pos;
            float dx = p.x - at.x, dz = p.z - at.z;
            if (dx * dx + dz * dz < best) {
                best = dx * dx + dz * dz;
                nearest = actor;
            }
        }
        return nearest;
    }

    // The running room of that zone: the owner of the actors it makes.
    Game::Room* RoomOfZone(s32 zone) {
        Game::Framework* fmwk = Game::GetFramework();
        if (zone < 0 || Game::SceneOf(fmwk) != Game::Scene::Level) return nullptr;
        auto* scene = static_cast<Game::LevelScene*>(fmwk->gameState->scene);
        Game::RoomManager* manager = scene->roomManager.get();
        if (manager == nullptr) return nullptr;
        for (Game::Room* room : manager->rooms) {
            if (room != nullptr && room->zone == zone && !room->aborted && room->root.ptr != nullptr) return room;
        }
        return nullptr;
    }

    // The chest's Zol: the game's own (the enemy randomizer's pick with Randomize Enemies).
    bool PopFromChest(Game::Actor* chest) {
        Shared<Game::Actor> enemy;
        Game::ObjTreasureBox_PopEnemy(enemy.Reset(), chest);
        if (!enemy) return false;
#ifdef LASAP_TEST
        g_From = "chest";
        KeepSpawned(0, enemy);
        for (size_t i = 1; i < kKept; i++) g_Zols[i] = {};
#endif
        return true;
    }

    // A green Zol at pos in the room of that zone, owned by the room as the chest's is, so it goes with the room.
    bool SpawnZol(s32 zone, const Game::Vec3& pos, size_t slot) {
        Game::Room* room = RoomOfZone(zone);
        void* rot = *Game::IdentityQuat_ptr;
        void* scale = *Game::OneScale_ptr;
        if (room == nullptr || rot == nullptr || scale == nullptr) return false;
        Game::ScopedActorParams made;
        Game::ActorParams& params = made.params;
        params.actorId = kZolGreen;
        params.zone = zone;
        params.pos = {pos.x, pos.y, pos.z, 0};
        std::memcpy(&params.rot, rot, sizeof(params.rot));
        std::memcpy(&params.scale, scale, sizeof(params.scale));
        Shared<Game::Actor> enemy;
        Game::Actor_Create(enemy.Reset(), &room->root, &params);
        if (!enemy) return false;
#ifdef LASAP_TEST
        KeepSpawned(slot, enemy);
#endif
        return true;
    }

    // The chest's Zol. On the overworld it is made between the chest and Link (in front of the chest), as high as
    // the chest makes it.
    bool ChestZol(Game::Actor* chest, const Game::Actor& link) {
        if (g_LastLevel != kOverworld) return PopFromChest(chest);
        if (chest->root == nullptr || link.root == nullptr) return false;
        const Game::Vec3& c = chest->root->coords.pos;
        const Game::Vec3& l = link.root->coords.pos;
        if (!SpawnZol(chest->zone, {(c.x + l.x) / 2, l.y + kAboveFloor, (c.z + l.z) / 2}, 0)) return false;
#ifdef LASAP_TEST
        g_From = "chest";
        for (size_t i = 1; i < kKept; i++) g_Zols[i] = {};
#endif
        return true;
    }

    // Zols beside Link, in his zone.
    int SpawnBesideLink(const Game::Actor& link) {
        if (link.root == nullptr) return 0;
        Game::Vec3 at = link.root->coords.pos;
        int made = 0;
        for (size_t i = 0; i < kKept; i++) {
            made += SpawnZol(link.zone, {at.x + kBesideLink[i], at.y + kAboveFloor, at.z}, i);
        }
#ifdef LASAP_TEST
        if (made > 0) g_From = "link";
#endif
        return made;
    }
}

    void NoteChestFind(LocationId location) {
        auto* player = Game::GetPlayer();
        Game::Actor* chest = player != nullptr ? ChestBesideLink(player->actor) : nullptr;
        if (chest == nullptr) return;
        g_FindChest = Weak<Game::Actor>(chest, chest->entity.self.ctrl);
        g_FindLocation = location;
    }

    // From the chest of its location while it still stands in Link's zone (he may have left it as the trap waited
    // to show), else beside Link.
    bool SpawnZolTrap(std::optional<LocationId> location) {
#ifdef LASAP_TEST
        g_From = "-";
        g_Spawned++;
        for (auto& zol : g_Zols) zol = {};
#endif
        auto* player = Game::GetPlayer();
        if (player == nullptr) return false;
        Game::Actor* chest = location && location == g_FindLocation ? Live(g_FindChest) : nullptr;
        if (chest != nullptr && chest->zone == player->actor.zone) {
            g_FindChest = {};
            g_FindLocation.reset();
            if (ChestZol(chest, player->actor)) {
                Logging.Log("[AP] zol trap: from its chest");
                return true;
            }
        }
        int made = SpawnBesideLink(player->actor);
        Logging.Log("[AP] zol trap: %d beside Link", made);
        return made > 0;
    }

#ifdef LASAP_TEST
    std::string ZolTrapState() {
        std::string s = std::string(" from=") + g_From + " spawned=" + std::to_string(g_Spawned);
        std::string zols;
        int alive = 0;
        for (const auto& zol : g_Zols) {
            Game::Actor* actor = Live(zol);
            if (actor == nullptr) continue;
            alive++;
            char text[80];
            Game::Vec3 p = actor->root != nullptr ? actor->root->coords.pos : Game::Vec3{0, 0, 0};
            snprintf(text, sizeof(text), " %x@%.2f,%.2f,%.2f/%d", actor->actorId, p.x, p.y, p.z, actor->zone);
            zols += text;
        }
        return s + " alive=" + std::to_string(alive) + zols;
    }
#endif
}
