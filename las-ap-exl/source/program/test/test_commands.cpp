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
            if (IsTrapKey(a[2])) QueueTrap(a[2], "", "");
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
            auto* player = Game::GetFramework()->player;
            auto* col = player->playerCollision;
            Game::Vec3 target{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
            col->base.node.coordsNew.pos = target;
            col->base.node.coords.pos = target;
            col->base.node.needsCoordUpdate = true;
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
            // DBG music: the jukebox's STATE fields and the last BGM requests
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
            // DBG peek <p|f|i|b|m|h> <hex offset> <hex length>: bytes of the player, the framework, the inventory, the BGM
            // manager, its first playing instance or that instance's sound
            Game::Framework* fmwk = Game::GetFramework();
            const u8* base = nullptr;
            if (a[2] == "p") base = fmwk ? reinterpret_cast<const u8*>(fmwk->player) : nullptr;
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
            // DBG enemies [reset]: enemies seen, changed and resized, and the last few as <vanilla id>><id>@<scale%>
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
}
#endif
