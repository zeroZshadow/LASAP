// Received items (DESIGN.md, Received items): granting an item the way a pickup would, the received count, Secret
// Medicines that wait, and the locations that keep their vanilla item.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "game/data/inventory.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/hooks/inventory.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include <sstream>

namespace AP::Bridge {
namespace {
    using Game::NowMs;

    constexpr int kRecvBits = 16;
    // Bits for received heart pieces and seashells (the game has 32 and 50), less those of the seed's vanilla sources
    // (AP::VanillaBits). Item actors ignore the inventory (kRandoFixes in patches.hpp), so receiving never hides one; the
    // game counts over all 64 bits.
    constexpr int kHeartPieceBits = 32;
    constexpr int kSeashellBits = 50;
    // Heart containers: Inventory_AddItemID takes indices 0-8 (scripts/re/ITEMS.md).
    constexpr int kHeartContainerBits = 9;
    // Chamber stones: received ones take the stones the Items page counts (PanelStone's mItemMenuCount: 3, 4, 8,
    // 10-13, 20-26), each a chamber panel Dampé makes at his next talk (scripts/re/ITEMS.md, Chamber stones).
    constexpr u64 kChamberStoneBits = 0x7f03d18;

    template<typename T>
    int LowestClearBit(T bits, int limit) {
        for (int i = 0; i < limit; i++) {
            if (!((bits >> i) & 1)) return i;
        }
        return limit - 1;
    }

    // The lowest bit of allowed that is clear in bits, -1 for none.
    int LowestClearBitOf(u64 bits, u64 allowed) {
        u64 free = allowed & ~bits;
        return free == 0 ? -1 : __builtin_ctzll(free);
    }

    // The bottle slots Link has (Bottle0..2) as bits 0..2: an added bottle fills the slot of its index.
    u64 BottleBits() {
        const auto& main = Game::Data::Inventory::Items()->MainItems;
        return main.Bottle0 | main.Bottle1 << 1 | main.Bottle2 << 2;
    }

    // Secret Medicines waiting (AP::MEDICINE_FLAG): the game holds one at a time (scripts/re/ITEMS.md).
    constexpr u64 kMedicinePollMs = 500;
    u64 g_MedicinePolled = 0;

    void SetMedicineWaiting(int n) {
        for (int i = 0; i < AP::MEDICINE_FLAG_BITS; i++) {
            EventFlags::SetFlag(AP::MEDICINE_FLAG + std::to_string(i), (n >> i) & 1);
        }
    }

    std::vector<KeptLocation> g_Kept;
    // The last such location collected by adding its item, and when: names a find of that item (Instant Pickups).
    std::optional<ItemId> g_KeptAddedItem;
    std::optional<LocationId> g_KeptAddedLocation;
    u64 g_KeptAddedAt = 0;
    constexpr u64 kKeptFlagPollMs = 500;
    u64 g_KeptFlagsPolled = 0;
    bool g_Granting = false;  // GrantItem adds a received item

    // Whether the save holds a kept location's item, as the Trendy Game decides whether to offer a prize: the bit of
    // its index, or for the Yoshi Doll (no index) its getting flag, TradeYoshiDollGet.
    bool SaveHasKept(const KeptLocation& kept) {
        auto* items = Game::Data::Inventory::Items();
        u64 bit = kept.index >= 0 && kept.index < 64 ? u64(1) << kept.index : 0;
        switch (*kept.item) {
            case ItemId::Seashell: return items->Seashells & bit;
            case ItemId::HeartPiece: return items->HeartPieces & bit;
            case ItemId::PanelDungeonPiece: return ChamberStoneBits() & bit;
            default: return EventFlags::CheckGettingFlag(*kept.item);
        }
    }
}

    int GetRecvCount() {
        int n = 0;
        for (int i = 0; i < kRecvBits; i++) {
            if (EventFlags::CheckFlag("APRecv" + std::to_string(i))) n |= (1 << i);
        }
        return n;
    }

    void SetRecvCount(int n) {
        for (int i = 0; i < kRecvBits; i++) {
            EventFlags::SetFlag("APRecv" + std::to_string(i), (n >> i) & 1);
        }
    }

    std::optional<ItemId> ItemIdFromKey(const std::string& key) {
        for (const auto& [id, name] : Inventory::kItemNames) {
            if (name == key) return id;
        }
        return std::nullopt;
    }

    bool IsTrapKey(const std::string& key) { return key.ends_with("Trap") || key == "$ENEMY"; }

    // The chamber stones owned (PanelPieceBits), bit i for stone i.
    u64 ChamberStoneBits() {
        const Game::BitTable* table = Game::PanelPieceBits.Ptr();
        u64 owned = 0;
        for (int i = 0; table->bits != nullptr && i < table->bytes && i < 8; i++) owned |= u64(table->bits[i]) << (8 * i);
        return owned;
    }

    void Damage(int quarterHearts) {
        auto* items = Game::Data::Inventory::Items();
        items->Health = items->Health > quarterHearts ? items->Health - quarterHearts : 1;
    }

    int GetMedicineWaiting() {
        int n = 0;
        for (int i = 0; i < AP::MEDICINE_FLAG_BITS; i++) {
            if (EventFlags::CheckFlag(AP::MEDICINE_FLAG + std::to_string(i))) n |= 1 << i;
        }
        return n;
    }

    // Grants one item the way an in-game pickup would, including LASR's progressive items.
    // Returns the item granted, none for traps (QueueTrap) and unknown keys.
    std::optional<ItemId> GrantItem(const std::string& key, int count, int dungeon) {
        auto* items = Game::Data::Inventory::Items();
        std::string k = key;
        if (IsTrapKey(k)) return std::nullopt;
        // LASR's "$EXT:" keys only name a chest text (Master Stalfos' Note); "None" is an item the client has no key for.
        if (k.starts_with("$EXT:") || k == "None") return std::nullopt;
        if (k == "SwordLv1" && EventFlags::CheckFlag("SwordFoundFlag")) k = "SwordLv2";
        else if (k == "Shield" && EventFlags::CheckFlag("ShieldFoundFlag")) k = "MirrorShield";
        else if (k == "PowerBraceletLv1" && EventFlags::CheckFlag("BraceletFoundFlag")) k = "PowerBraceletLv2";
        else if (k == "ClothesRed") EventFlags::SetFlag("RedTunicFoundFlag", true);
        else if (k == "ClothesBlue") EventFlags::SetFlag("BlueTunicFoundFlag", true);

        std::optional<ItemId> found = ItemIdFromKey(k);
        if (!found) {
            AP::Log("unknown item key " + key);
            return std::nullopt;
        }
        ItemId id = *found;

        int index = -1;
        switch (id) {
            case ItemId::Seashell:
                index = LowestClearBit(items->Seashells | AP::VanillaBits('S'), kSeashellBits);
                break;
            case ItemId::GoldenLeaf:
                index = LowestClearBit(items->GoldenLeaves | AP::VanillaBits('L'), 5);
                break;
            case ItemId::HeartPiece:
                index = LowestClearBit(items->HeartPieces | AP::VanillaBits('H'), kHeartPieceBits);
                break;
            case ItemId::HeartContainer:
                index = LowestClearBit(items->HeartContainers | AP::VanillaBits('C'), kHeartContainerBits);
                break;
            case ItemId::Bottle:  // the bottle slot
                index = LowestClearBit(BottleBits() | AP::VanillaBits('B'), 3);
                break;
            case ItemId::PanelDungeonPiece:
                index = LowestClearBitOf(ChamberStoneBits() | AP::VanillaBits('P'), kChamberStoneBits);
                break;
            case ItemId::Compass: case ItemId::DungeonMap: case ItemId::StoneBeak: case ItemId::SmallKey:
            case ItemId::NightmareKey:
                index = dungeon;
                break;
            default: break;
        }
        // A Secret Medicine while Link holds one waits (TickMedicine); its item-get shows now.
        if (id == ItemId::SecretMedicine && items->ExtraItems.Medicine) {
            int waiting = GetMedicineWaiting();
            if (waiting < (1 << AP::MEDICINE_FLAG_BITS) - 1) SetMedicineWaiting(waiting + 1);
            AP::Log("Secret Medicine waits (" + std::to_string(GetMedicineWaiting()) + ")");
            return id;
        }
        g_Granting = true;
        Inventory::AddItemID(id, count, index);
        g_Granting = false;
        return id;
    }

    // Gives a waiting Secret Medicine once Link, free and alive, has none (he used his, or it was never given).
    void TickMedicine() {
        auto* items = Game::Data::Inventory::Items();
        if (items == nullptr || items->ExtraItems.Medicine || NowMs() - g_MedicinePolled < kMedicinePollMs) return;
        g_MedicinePolled = NowMs();
        if (!PlayerReady() || !PlayerFree() || LinkDead()) return;
        int waiting = GetMedicineWaiting();
        if (waiting == 0) return;
        SetMedicineWaiting(waiting - 1);
        g_Granting = true;
        Inventory::AddItemID(ItemId::SecretMedicine, 1, -1);
        g_Granting = false;
        AP::Log("Secret Medicine given (" + std::to_string(waiting - 1) + " waiting)");
        QueueNotice("Secret Medicine|from your stock");
    }

    const KeptLocation* FindKept(LocationId location) {
        for (const auto& kept : g_Kept) {
            if (kept.location == location) return &kept;
        }
        return nullptr;
    }

    // The server's item for a location that keeps its vanilla item. The game gave it when the location was collected
    // in this save; a save without it (an older one, another file, a !collect) gets it now with the location's own
    // index, which also takes the prize out of the Trendy Game. A companion joins only in the game. Returns the item
    // to show, if any.
    std::optional<ItemId> GrantKept(const KeptLocation& kept) {
        if (!kept.flag.empty() || SaveHasKept(kept)) return std::nullopt;
        g_Granting = true;
        Inventory::AddItemID(*kept.item, 1, kept.index);
        g_Granting = false;
        return kept.item;
    }

    std::optional<LocationId> KeptAddedSince(ItemId item, u64 since) {
        return g_KeptAddedItem == item && g_KeptAddedAt >= since ? g_KeptAddedLocation : std::nullopt;
    }

    // "<item key> <index> <local id>" and "F <flag> <local id>" lines (patcher las_ap_patch.write_vanilla)
    void LoadKept() {
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(AP::VANILLA_PATH, text))) return;
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::stringstream ss(line);
            std::string key;
            if (line.starts_with("F ")) {
                std::string flag;
                LocationId location{};
                if (ss >> key >> flag >> location) g_Kept.push_back({std::nullopt, -1, location, flag});
                continue;
            }
            int index;
            LocationId location{};
            if (!(ss >> key >> index >> location)) continue;
            std::optional<ItemId> item = ItemIdFromKey(key);
            if (!item) {
                AP::Log("unknown kept item " + key);
                continue;
            }
            g_Kept.push_back({item, index, location, ""});
        }
        Logging.Log("[AP] kept locations: %zu", g_Kept.size());
    }

    // Reports a companion once the flag its joining sets is set.
    void TickKeptFlags() {
        if (!PlayerReady() || NowMs() - g_KeptFlagsPolled < kKeptFlagPollMs) return;
        g_KeptFlagsPolled = NowMs();
        for (const auto& kept : g_Kept) {
            if (!kept.flag.empty() && !CollectedHere(kept.location) && EventFlags::CheckFlag(kept.flag.c_str())) {
                RecordCheck(kept.location);
            }
        }
    }
}

namespace AP {
    using namespace Bridge;

    void OnItemAdded(ItemId item, int index) {
        if (g_Granting) return;
        for (const auto& kept : g_Kept) {
            if (kept.item != item || (kept.index >= 0 && kept.index != index)) continue;
            RecordCheck(kept.location);
            g_KeptAddedItem = item;
            g_KeptAddedLocation = kept.location;
            g_KeptAddedAt = NowMs();
        }
    }
}
