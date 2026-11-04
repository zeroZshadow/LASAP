// The models the AP locations show (DESIGN.md, Item models; scripts/re/ITEMS.md, Row strings, Item node, Model
// makers). A location's actors name its token as their model (patcher MODEL_TOKEN), the same in every seed, and
// the seed file's models name the model of the location's item: the module resolves the token where an NPC component reads its
// row's model, places the actor as LASR places one with that model, and gives the item-get's item node, the items
// NPCs hold up and the shop's shelf that item's model. A location's chests name its chest token as their model file,
// which the module resolves to the chest of the location's item, at its size.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_options.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_text.hpp"
#include "config/seed_file.hpp"
#include "game/actor.hpp"
#include "game/actors/npc.hpp"
#include "game/math.hpp"
#include "game/mem.hpp"
#include "game/sheet.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"
#include <charconv>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace AP::Bridge {
namespace {
    // The model of a location's item: its item id for the item-get (-1: the placeholder's), its model file and name
    struct Model {
        int itemId = -1;
        std::string_view file;     // in the seed file
        std::string_view name;
        std::string shelfPath;     // "actor/<file>", as ShopItem.gsheet names a model
    };
    // How LASR places an actor with that model, against one with the token (patcher ground_transform)
    struct Stance {
        float scale = 1.0f;        // a factor
        std::optional<float> yaw;  // degrees
        float rise = 0.0f;
    };
    std::map<LocationId, Model> g_Models;
    std::map<LocationId, Stance> g_Stances;  // the location's item actors'; its chests and drops keep theirs
    std::map<LocationId, std::string_view> g_Chests;  // a chest location's chest file
    std::map<LocationId, float> g_ChestSizes;  // a chest location's chest size, when not 1
    // LASR's Items.gsheet rows that stand in for a location's item: by item id and by key
    std::map<u8, LocationId> g_StandIns;
    std::map<std::string_view, LocationId> g_StandInKeys;

    constexpr size_t kLebRoomString = 0x50;  // LebRoom virtual (offset) -> its characters (ACTORS.md, LEB actor record)
    constexpr u32 kLebString = 4;            // a LEB parameter's type: a string by its offset in the LEB

    // ShopItem.gsheet (scripts/re/ITEMS.md, Shop shelf): a slot and the goods it shows in turn
    struct ShopGoods {
        Game::SheetString condition;
        Game::SheetString item;    // 0x10: an Items.gsheet key
        Game::SheetString modelPath;  // 0x20
        Game::SheetString modelName;  // 0x30
        s32 count;                 // 0x40
        s32 index;                 // 0x44
        Game::SheetString label;   // 0x48
        Game::SheetString entryPoint;  // 0x58
    };
    static_assert(offsetof(ShopGoods, modelPath) == 0x20 && sizeof(ShopGoods) == 0x68);
    struct ShopItemRow {
        s32 index;
        u8 pad_4[0x4];
        Game::SheetArray<ShopGoods> goods;  // 0x8
    };
    static_assert(sizeof(ShopItemRow) == 0x18);

    // The location a token names ("ap:<local id>", "apchest:<local id>")
    std::optional<LocationId> TokenLocation(std::string_view text, std::string_view token = AP::MODEL_TOKEN) {
        if (!text.starts_with(token)) return std::nullopt;
        text.remove_prefix(token.size());
        int id = 0;
        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
        if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
        return LocationId{id};
    }

    const Model* ModelOf(std::optional<LocationId> location) {
        if (!location) return nullptr;
        auto it = g_Models.find(*location);
        return it != g_Models.end() ? &it->second : nullptr;
    }

    // The item whose model and pose a held-up `item` shows: the location's item for a stand-in row, and for the
    // placeholder that of `placeholderAt`
    u8 ShownId(u8 item, std::optional<LocationId> placeholderAt) {
        std::optional<LocationId> location;
        if (item == std::to_underlying(AP::PLACEHOLDER_ITEM_ID)) {
            location = placeholderAt;
        } else if (auto it = g_StandIns.find(item); it != g_StandIns.end()) {
            location = it->second;
        }
        const Model* model = ModelOf(location);
        return model != nullptr && model->itemId >= 0 ? static_cast<u8>(model->itemId) : item;
    }

    // The item-get's item node, whose model and pose follow its item id (scripts/re/ITEMS.md, Item node). A
    // placeholder's location is the one added last: its flows add it right before its item-get.
    HOOK_DEFINE_TRAMPOLINE(ItemGetHolderMake) {
        static void Callback(Game::SharedPtr<>* out, void* args, u8 itemId) {
            Orig(out, args, ShownId(itemId, LastFoundLocation()));
        }
    };

    // The other items held up as models: the mansion's present, the NPCs that show what they give
    HOOK_DEFINE_TRAMPOLINE(ItemModelMake) {
        static void Callback(Game::SharedPtr<Game::ItemModelHolder>* out, Game::SharedPtr<>* owner,
                             Game::StrView* name, Game::SharedPtr<>* root, u8 itemId) {
            Orig(out, owner, name, root, ShownId(itemId, FoundOrPresentLocation()));
        }
    };

    // A row string an NPC component resolves: a token in its model file or model name becomes that of the location's
    // item, or the Archipelago model's; a chest token the location's chest file, or the plain chest's.
    HOOK_DEFINE_TRAMPOLINE(NpcResolveParam) {
        static void Callback(Game::StrView* out, Game::Actors::NpcComponent* component, const Game::StrView* in) {
            Orig(out, component, in);
            if (std::optional<LocationId> chest = TokenLocation(out->View(), AP::CHEST_TOKEN)) {
                auto it = g_Chests.find(*chest);
                *out = Game::StrView(it != g_Chests.end() ? it->second.data() : AP::PLAIN_CHEST_FILE);
                return;
            }
            std::optional<LocationId> location = TokenLocation(out->View());
            const Game::Actors::NpcRow* row = component->row;
            if (!location || row == nullptr || in == nullptr) return;
            // the callers pass a copy of the row's view: its characters are the field's
            bool file = in->data == row->graphics.path.data;
            if (!file && in->data != row->graphics.model.data) return;
            const Model* model = ModelOf(location);
            if (model != nullptr) {
                *out = Game::StrView(file ? model->file.data() : model->name.data());
            } else {
                *out = Game::StrView(file ? AP::PLACEHOLDER_MODEL_FILE : AP::PLACEHOLDER_MODEL_NAME);
            }
        }
    };

    // The location token among an actor's string LEB parameters, and whether it is a chest's (AP::CHEST_TOKEN)
    struct Token {
        LocationId location;
        bool chest;
    };
    std::optional<Token> ParamsToken(const Game::ActorParams& params) {
        if (params.lebRoom.ptr == nullptr) return std::nullopt;
        for (const Game::LebParam& param : params.lebParams) {
            if (param.type != kLebString) continue;
            const char* text = Game::CallVirtual<const char*(u32)>(params.lebRoom.ptr, kLebRoomString, param.value);
            if (text == nullptr) continue;
            if (std::optional<LocationId> location = TokenLocation(text)) return Token{*location, false};
            if (std::optional<LocationId> location = TokenLocation(text, AP::CHEST_TOKEN)) return Token{*location, true};
        }
        return std::nullopt;
    }

    // How an actor with a location's token stands: a chest at the size of the location's chest, an item actor as LASR
    // stands one with the location's model; a drop actor sizes its model itself (ap_drop.cpp), as LASR's stand-ins
    // took no item's size or turn.
    std::optional<Stance> StanceOf(const Game::ActorParams& params) {
        std::optional<Token> token = ParamsToken(params);
        if (!token) return std::nullopt;
        if (token->chest) {
            auto it = g_ChestSizes.find(token->location);
            return it != g_ChestSizes.end() ? std::optional(Stance{it->second}) : std::nullopt;
        }
        if (params.lebRecord != nullptr && params.lebRecord->actorId == AP::DROP_ACTOR_ID) return std::nullopt;
        auto it = g_Stances.find(token->location);
        return it != g_Stances.end() ? std::optional(it->second) : std::nullopt;
    }

    // An actor with a location's token stands as StanceOf says: params are built from the LEB record for each
    // creation, so the change applies once.
    HOOK_DEFINE_TRAMPOLINE(ActorCreate) {
        static void Callback(Game::SharedPtr<Game::Actor>* out, Game::SharedPtr<>* owner, Game::ActorParams* params) {
            std::optional<Stance> found = params != nullptr ? StanceOf(*params) : std::nullopt;
            if (found) {
                const Stance& stance = *found;
                params->scale.x *= stance.scale;
                params->scale.y *= stance.scale;
                params->scale.z *= stance.scale;
                params->pos.y += stance.rise;
                if (stance.yaw && params->lebRecord != nullptr) {
                    Game::Vec3 degrees = params->lebRecord->rotDegrees;
                    degrees.y = *stance.yaw;
                    Game::Quat q = Game::EulerQuat(degrees);
                    params->rot = {q.x, q.y, q.z, q.w};
                }
            }
            Orig(out, owner, params);
        }
    };

    // The shop's shelf shows a stand-in row's goods with the model of the location's item
    void ShowShelfModels(const Game::SheetData<ShopItemRow>* data) {
        int shown = 0;
        for (u32 i = 0; i < data->count; i++) {
            const Game::SheetArray<ShopGoods>& goods = data->rows[i].goods;
            for (u32 j = 0; j < goods.count; j++) {
                auto& entry = const_cast<ShopGoods&>(goods.data[j]);
                auto it = g_StandInKeys.find(entry.item.View());
                const Model* model = it != g_StandInKeys.end() ? ModelOf(it->second) : nullptr;
                if (model == nullptr) continue;
                entry.modelPath = {model->shelfPath.c_str(), static_cast<u32>(model->shelfPath.size()), {}};
                entry.modelName = {model->name.data(), static_cast<u32>(model->name.size()), {}};
                shown++;
            }
        }
        Logging.Log("[AP] shop shelf models: %d", shown);
    }

    // Every sheet the game makes: the shop's shelf, and the sheets of the options (ap_options.cpp)
    HOOK_DEFINE_TRAMPOLINE(DatasheetCreate) {
        static Game::Shared<> Callback(void* unused, const void* info) {
            Game::Shared<> made = Orig(unused, info);
            auto* sheet = static_cast<Game::Sheet<ShopItemRow>*>(made.get());
            const Game::SheetData<ShopItemRow>* data = sheet != nullptr ? sheet->data : nullptr;
            if (data == nullptr || data->name == nullptr) return made;
            std::string_view name = data->name;
            if (name == "ShopItem") {
                if (!g_StandInKeys.empty()) ShowShelfModels(data);
            } else {
                Options::EditSheet(name, sheet);
            }
            return made;
        }
    };

    // The seed file's models: "M <local id> <item id> <model file> <model name>" the model of the location's item
    // (item id -1: the item-get holds up the placeholder's), "S <local id> <scale factor> <yaw or -> <rise>" how its
    // actors stand otherwise, "I <row key> <item id> <local id>" an Items.gsheet row that stands in for its item, "C
    // <local id> <size> <chest file>" the chest of its item (patcher model_lines)
    void LoadModels() {
        SeedFile::ForEach("models", [](Fields& f) {
            std::string_view kind = f.Str();
            if (kind == "M") {
                auto location = f.Num<LocationId>();
                auto itemId = f.Num<int>();
                Model model{itemId.value_or(-1), f.Str(), f.Str(), {}};
                if (!location || model.name.empty()) return;
                model.shelfPath = "actor/" + std::string(model.file);
                g_Models[*location] = std::move(model);
            } else if (kind == "S") {
                auto location = f.Num<LocationId>();
                auto scale = f.Num<float>();
                Fields yawField = f;  // the yaw as a number, unless it is "-"
                bool noYaw = f.Str() == "-";
                std::optional<float> yaw = noYaw ? std::nullopt : yawField.Num<float>();
                auto rise = f.Num<float>();
                if (!location || !scale || !rise || (!noYaw && !yaw)) return;
                g_Stances[*location] = {*scale, yaw, *rise};
            } else if (kind == "I") {
                std::string_view key = f.Str();
                auto itemId = f.Num<int>();
                auto location = f.Num<LocationId>();
                if (!itemId || !location) return;
                g_StandIns[static_cast<u8>(*itemId)] = *location;
                g_StandInKeys[key] = *location;
            } else if (kind == "C") {
                auto location = f.Num<LocationId>();
                auto scale = f.Num<float>();
                std::string_view file = f.Str();
                if (!location || !scale || file.empty()) return;
                g_Chests[*location] = file;
                if (*scale != 1.0f) g_ChestSizes[*location] = *scale;
            }
        });
        Logging.Log("[AP] models: %zu shown, %zu placed, %zu stand-ins, %zu chests", g_Models.size(),
                    g_Stances.size(), g_StandIns.size(), g_Chests.size());
    }
}

    std::optional<std::pair<std::string_view, std::string_view>> TokenModel(const Game::ActorParams& params) {
        std::optional<Token> token = ParamsToken(params);
        if (!token) return std::nullopt;
        const Model* model = ModelOf(token->location);
        if (model == nullptr) return std::pair<std::string_view, std::string_view>{PLACEHOLDER_MODEL_FILE,
                                                                                   PLACEHOLDER_MODEL_NAME};
        return std::pair{model->file, model->name};
    }

    ItemId ShownItem(ItemId item) {
        return ItemId{ShownId(static_cast<u8>(item), LastFoundLocation())};
    }

    void InstallModelHooks() {
        LoadModels();
        NpcResolveParam::InstallAtFuncPtr(Game::NpcComponent_ResolveParam.Ptr());
        ItemGetHolderMake::InstallAtFuncPtr(Game::ItemGetHolder_Make.Ptr());
        ItemModelMake::InstallAtFuncPtr(Game::ItemModel_Make.Ptr());
        if (!g_Stances.empty() || !g_ChestSizes.empty()) ActorCreate::InstallAtFuncPtr(Game::Actor_Create.Ptr());
        DatasheetCreate::InstallAtFuncPtr(Game::Datasheet_Create.Ptr());
    }
}
