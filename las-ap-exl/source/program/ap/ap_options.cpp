// The options the RomFS holds both ways (DESIGN.md, Options): config.ini's [options] picks one at runtime. Events,
// sheet conditions and NPC layouts ask option flags (EventFlags_Check, game/hooks/eventflags.cpp), rooms leave out,
// retype or move LEB actors (Room_OnLebActor; ACTORS.md, Rooms), and the sheets of some options take their values
// when the game makes them (Datasheet_Create).
#include "ap/ap_options.hpp"
#include "ap/ap_patcher.hpp"
#include "config/config.hpp"
#include "game/actor.hpp"
#include "game/sheet.hpp"
#include "game/sym.hpp"
#include "lib.hpp"
#include "loggers.hpp"
#include <array>
#include <cstring>
#include <utility>

namespace {
    constexpr std::string_view kOptPrefix = "APOpt.";
    constexpr std::string_view kIfPrefix = "APIf|";

    // A condition by Conditions_Check's type and argument; the argument is copied to be terminated.
    bool Holds(std::string_view type, std::string_view argument) {
        if (type == "0") return argument != "false";
        std::array<char, 96> text{};
        if (argument.size() >= text.size()) return false;
        std::memcpy(text.data(), argument.data(), argument.size());
        Game::StrView view(text.data(), argument.size());
        u32 kind = 0;
        for (char c : type) kind = kind * 10 + static_cast<u32>(c - '0');
        return Game::Conditions_Check(nullptr, kind, &view, nullptr);
    }

    // The next '|'-separated field of text, taken off its front
    std::string_view Field(std::string_view& text) {
        size_t bar = text.find('|');
        std::string_view field = text.substr(0, bar);
        text.remove_prefix(bar == std::string_view::npos ? text.size() : bar + 1);
        return field;
    }

    // ---- Room actors ----

    enum class Edit { Skip, Retype, Move };
    struct ActorEdit {
        std::string_view option;
        bool when;                 // the option's value that makes the edit
        u64 key;
        Edit edit;
        u16 actorId = 0;           // Retype
        float x = 0, z = 0;        // Move: the place
    };
    constexpr ActorEdit kActorEdits[] = {
        // Open Mabe: the grass, rocks and monsters around Mabe Village (LASR's RoomFixes.openMabe)
        {"open_mabe", true, 0x624A97005CD29205, Edit::Skip},
        {"open_mabe", true, 0x62000A005D15AC9E, Edit::Skip},
        {"open_mabe", true, 0x620015005D15AC9E, Edit::Skip},
        {"open_mabe", true, 0x7200BB005CFF3740, Edit::Skip},
        {"open_mabe", true, 0x7200B9005CFF3740, Edit::Skip},
        {"open_mabe", true, 0x7200DC005CFF3741, Edit::Skip},
        {"open_mabe", true, 0x7200D6005CFF3741, Edit::Skip},
        // The randomized enemies: the Armos statues over the stairs (LASR's RoomFixes.openArmosStairs)
        {"randomize_enemies", true, 0x610250005BA1C308, Edit::Skip},
        {"randomize_enemies", true, 0x6102FE005BA1C30B, Edit::Skip},
        // Classic D2: the rock before Bottle Grotto is a swamp flower, EnemyMonea (Field_03E)
        {"classic_d2", true, 0x6A0012005D0C5FB9, Edit::Retype, 0x0E},
        // Completed Bridge: Kiki and the stick stand aside (Field_08L, LASR's TradeQuestRandomizer)
        {"completed_bridge", true, 0x750014005CF52527, Edit::Move, 0, 173.25f, 91.0f},
        {"completed_bridge", true, 0x750013005CF52527, Edit::Move, 0, 173.219f, 91.855f},
        // The Ammo Ghost in Tarin's house (patcher ap_ghost.ACTOR_KEY)
        {"ammo_ghost", false, 0x6D0040005CEDEAC0, Edit::Skip},
    };

    // A drop that a chest or an owl statue launches, made only with Bouncy Items (patcher ap_drop._launched_drop)
    bool LaunchedDrop(const Game::LebActorRecord* record) {
        u32 motion = record->params[0].value;
        return record->actorId == AP::DROP_ACTOR_ID && (motion == AP::DROP_CHEST || motion == AP::DROP_OWL);
    }

    HOOK_DEFINE_TRAMPOLINE(RoomOnLebActor) {
        static void Callback(void* room, void* leb, Game::LebActorRecord* record) {
            if (record != nullptr) {
                if (LaunchedDrop(record) && !global_config.Option("bouncy_items")) return;
                for (const ActorEdit& e : kActorEdits) {
                    if (e.key != record->key || global_config.Option(e.option) != e.when) continue;
                    if (e.edit == Edit::Skip) return;
                    // The record is the level's loaded LEB: a write lasts, so it sets values, not offsets
                    if (e.edit == Edit::Retype) record->actorId = e.actorId;
                    if (e.edit == Edit::Move) record->pos.x = e.x, record->pos.z = e.z;
                }
            }
            Orig(room, leb, record);
        }
    };

    // ---- Sheets ----

    struct FishRow {               // FishingFish.gsheet
        s32 id;
        s32 actorId;               // 0x4
        Game::SheetString openItem;  // 0x8: what its catch gives
        u8 pad_18[0x38];
    };
    static_assert(sizeof(FishRow) == 0x50);
    struct SpotRow {               // FishingSpot.gsheet
        s32 id;
        s32 spot;                  // 0x4
        Game::SheetArray<s32> weights;  // 0x8
    };
    static_assert(sizeof(SpotRow) == 0x18);
    struct DropLot {
        Game::SheetString type;    // an Items.gsheet key
        s32 cookie;                // 0x10
        s32 weight;                // 0x14
    };
    static_assert(sizeof(DropLot) == 0x18);
    struct DropRow {               // ItemDrop.gsheet
        Game::SheetString key;
        s32 kodamaKey;             // 0x10
        u8 pad_14[0x4];
        Game::SheetArray<DropLot> lots;  // 0x18
        u8 pad_28[0x18];
    };
    static_assert(sizeof(DropRow) == 0x40);

    // Bouncy Items: the drop's row (AP::DROP_ACTOR) takes the ball's (patcher ap_drop.BALL_ROW), its name kept: the
    // sheet's rows by key give the row's index
    constexpr std::string_view kBallRow = "ItemAPDropBall";
    void SwapDropRows(void* made) {
        const auto* data = static_cast<Game::Sheet<Game::SheetString>*>(made)->data;
        if (data == nullptr) return;
        auto* rows = reinterpret_cast<u8*>(const_cast<Game::SheetString*>(data->rows));
        u8 *drop = nullptr, *ball = nullptr;
        for (u32 i = 0; i < data->count; i++) {
            auto* row = rows + static_cast<size_t>(i) * data->rowSize;
            std::string_view symbol = reinterpret_cast<const Game::SheetString*>(row)->View();
            if (symbol == AP::DROP_ACTOR) drop = row;
            else if (symbol == kBallRow) ball = row;
        }
        if (drop == nullptr || ball == nullptr) return;
        constexpr size_t kSymbol = sizeof(Game::SheetString);  // the row's first field
        for (size_t i = kSymbol; i < data->rowSize; i++) std::swap(drop[i], ball[i]);
        Logging.Log("[AP] options: the drop is a ball");
    }

    // Easy Fishing: Ol' Baron bites more often in the deep spot (row 4)
    constexpr s32 kEasyFishingSpot = 4;
    constexpr s32 kEasyFishingWeights[] = {0, 15, 0, 0, 15, 0};

    void SetString(const Game::SheetString& field, const char* text) {
        auto& s = const_cast<Game::SheetString&>(field);
        s.data = text;
        s.length = static_cast<u32>(std::strlen(text));
    }

    template<typename Row, typename F>
    void ForRows(void* made, F&& f) {
        const auto* data = static_cast<Game::Sheet<Row>*>(made)->data;
        if (data == nullptr || data->rowSize != sizeof(Row)) {
            Logging.Log("[AP] options: sheet row size %u, not %zu", data != nullptr ? data->rowSize : 0, sizeof(Row));
            return;
        }
        for (u32 i = 0; i < data->count; i++) f(const_cast<Row&>(data->rows[i]));
    }
}

namespace AP::Options {
    std::optional<bool> Flag(std::string_view name) {
        if (name.starts_with(kOptPrefix)) return global_config.Option(name.substr(kOptPrefix.size()));
        if (!name.starts_with(kIfPrefix)) return std::nullopt;
        std::string_view rest = name.substr(kIfPrefix.size());
        bool on = global_config.Option(Field(rest));
        std::string_view type = Field(rest), argument = Field(rest);
        if (!on) type = Field(rest), argument = Field(rest);
        return Holds(type, argument);
    }

    void InstallHooks() { RoomOnLebActor::InstallAtFuncPtr(Game::Room_OnLebActor.Ptr()); }

    void EditSheet(std::string_view name, void* sheet) {
        const auto& tweaks = global_config.tweaks;
        // Fast Fishing: every fish that gives an item gives the green tunic, which the game turns into the item
        // its row names
        if (name == "FishingFish" && global_config.Option("fast_fishing")) {
            ForRows<FishRow>(sheet, [](FishRow& row) {
                if (row.openItem.length > 0) SetString(row.openItem, "ClothesGreen");
            });
        } else if (name == "FishingSpot" && tweaks.easy_fishing) {
            ForRows<SpotRow>(sheet, [](SpotRow& row) {
                if (row.id != kEasyFishingSpot || row.weights.count != std::size(kEasyFishingWeights)) return;
                std::memcpy(const_cast<s32*>(row.weights.data), kEasyFishingWeights, sizeof(kEasyFishingWeights));
            });
        } else if (name == "Npc" && global_config.Option("bouncy_items")) {
            SwapDropRows(sheet);
        } else if (name == "ItemDrop" && (tweaks.no_guardian_acorn || tweaks.no_piece_of_power)) {
            // A disabled buff drops nothing, as LASR does for the drops it removes
            ForRows<DropRow>(sheet, [&](DropRow& row) {
                bool off = (row.key.View() == "GuardianAcorn" && tweaks.no_guardian_acorn) ||
                           (row.key.View() == "PowerPiece" && tweaks.no_piece_of_power);
                if (off && row.lots.count > 0) SetString(row.lots.data[0].type, "");
            });
        }
    }
}
