// The puppets' look (DESIGN.md, Ghosts): the materials and shapes a ghost draws, its tunic and its face.
#include "game/mem.hpp"
#include "game/model.hpp"
#include "game/sym.hpp"
#include "ghost/ghost_puppets_impl.hpp"
#include <algorithm>
#include <string_view>

namespace AP::Puppets {
namespace {
    using Game::At;
    using Game::SharedPtr;
    using Game::StrView;

    // Link's materials drawn for a ghost: his body and the items on his model. Which of its shapes draw is the
    // ghost's look (SetGear).
    const char* const kShownMaterials[] = {"MI_body", "MI_boots", "MI_ear", "MI_eye", "MI_mouth", "MI_hair",
                                           "linkShieldA_MI_shieldA", "linkSwordA_MI_sowrdA", "linkSwordA_MI_sowrdAball",
                                           "MI_flippers", "MagicRod_MI_magicRod", "Ocarina_MI_ocarina", "Shovel_MI_shovel",
                                           "linkHookShot_MI_hookShot", "linkShieldB_MI_shieldB",
                                           "linkShieldB_MI_shieldBMirror", "linkSwordB_MI_sowrdB",
                                           "linkSwordB_MI_sowrdBball"};
    // The look's tunic: the material animation "clothes" swaps MI_body's albedo (frames 0 green, 1 red, 3 blue).
    // NpcComponent_PlayMaterialAnimation (0x9c1b30: out shared_ptr, NPC component, {name, length}*, at once) puts it
    // on a slot of the NPC's material animation component (NPC component +0x1b8; slots +0xa0, count +0xb8; a slot's
    // +0x8 its animation); the slot is the facial ones' count (NPC component +0x198 -> +0xf0: +0x38, one more each
    // for +0x18 and +0x28 set) unless +0x198 is missing or its +0x43 bit 6 is set, then 0. Slot virtuals +0x48 set
    // frame, +0x60 set rate.
    constexpr size_t kNpcFacial = 0x198, kFacialFlags = 0x43, kFacialSet = 0xf0, kFacialCount = 0x38,
                     kFacialExtraA = 0x18, kFacialExtraB = 0x28;
    constexpr size_t kNpcMaterialAnim = 0x1b8, kMatAnimSlots = 0xa0, kMatAnimSlotCount = 0xb8, kSlotAnim = 0x8;
    constexpr size_t kSlotSetFrame = 0x48, kSlotSetRate = 0x60;
    constexpr float kTunicFrames[] = {0, 1, 3};
    // The face: Link's eyes, mouth and ears are material animations held at a frame ("eye", "mouth", "ear"). The
    // APGhost row's facial row (Facial.gsheet, ap_player_ghost.py) names them, so the NPC's material animation
    // component has a slot for each (before the tunic's), at frame 0 with no blinking; the module finds each by its
    // animation's name (slot +0x8, its resource at +0x18, the name at +0x8) and holds its frame (+0x24).
    // The instanced face (Instanced models) draws as the model draws without these animations, which is frame 0 of
    // each; a ghost with another face draws its own face shapes, the frames set on its slots.
    constexpr size_t kMatAnimRes = 0x18, kResName = 0x8, kSlotFrame = 0x24;
    constexpr const char* kFaceAnims[] = {"eye", "mouth", "ear"};
    constexpr int kFaceFrames[kFaceParts] = {14, 14, 2};  // the FMAAs' frames

    // The model's visibility words a ghost draws with: the materials it shows (kShownMaterials).
    void ComputeShownMats(Puppet& p) {
        u16 count = std::min<u16>(At<u16>(p.model, kModelMatCount), 32 * 4);
        const u8* mats = At<const u8*>(p.model, kModelMats);
        std::fill(std::begin(p.shownMats), std::end(p.shownMats), 0);
        for (u16 i = 0; i < count; i++) {
            std::string_view name;
            if (const u8* res = At<const u8*>(mats + i * kMatStride, 0)) {
                name = Game::ResName(At<const u8*>(res, 8));
            }
            auto named = [&](const char* m) { return name == m; };
            if (std::any_of(std::begin(kShownMaterials), std::end(kShownMaterials), named))
                p.shownMats[i / 32] |= 1u << (i % 32);
        }
        p.matWords = (count + 31) / 32;
    }

    // The slot of the puppet's material animation component that holds the animation of that name, or -1.
    int MatSlotOf(const Puppet& p, std::string_view name) {
        const u8* mat = At<const u8*>(AnimComp(p), kNpcMaterialAnim);
        const u8* const* slots = At<const u8* const*>(mat, kMatAnimSlots);
        int count = At<s32>(mat, kMatAnimSlotCount);
        for (int i = 0; slots != nullptr && i < count; i++) {
            const u8* s = At<const u8*>(At<const u8*>(At<const u8*>(slots[i], kSlotAnim), kMatAnimRes), kResName);
            if (s != nullptr && Game::ResName(s) == name) return i;
        }
        return -1;
    }
}

    // Draws only the materials a ghost shows, or (hidden) none; true when they already were. The game sets the
    // visibility again at times (the model's setup some frames after it appears, the actor coming back after a
    // dungeon room's scroll), so it is checked every frame.
    bool WriteLook(Puppet& p, bool draw) {
        if (p.model == nullptr) return false;
        if (p.matWords < 0) ComputeShownMats(p);
        u16 count = std::min<u16>(At<u16>(p.model, kModelMatCount), 32 * 4);
        u32* visible = At<u32*>(p.model, kModelMatVis);
        if (visible == nullptr) return false;
        bool kept = true;
        for (int w = 0; w < p.matWords; w++) {
            u32 valid = count - 32 * w >= 32 ? ~0u : (1u << (count - 32 * w)) - 1;
            u32 want = (draw ? p.shownMats[w] : 0) | (visible[w] & ~valid);
            if (visible[w] != want) {
                visible[w] = want;
                kept = false;
            }
        }
#ifdef LASAP_TEST
        if (!kept) {
            p.visFixes++;
            if (p.animLog.size() < 40)
                p.animLog.push_back(std::string(draw ? "vis" : "hide") + "@" + std::to_string(g_LastFrame));
        }
#endif
        return kept;
    }

    // The look's shapes on the puppet's model.
    void SetShapes(Puppet& p) {
        u8* instance = PlayerInstance(At<void*>(AnimComp(p), kAnimCompPlayer));
        if (At<u8*>(At<u8*>(instance, kInstanceModel), kModelObject) != p.model) return;
        u8* shapes = At<u8*>(instance, kInstanceShapes);
        if (shapes == nullptr) return;
        int count = std::min<int>(At<u16>(p.model, kModelShapeCount), AP::GhostNet::SHAPE_COUNT);
        u32 mask = p.detail == 2 ? g_ShapeMask : g_ShapeMask & g_CrowdMask;
        mask &= ~(kInstancedShapes & ~(p.ownFace ? kFaceShapes : 0));
        for (int i = 0; i < count; i++) shapes[i] = (p.look.shapes & mask) >> i & 1;
    }

    void* TunicSlot(const Puppet& p, int& index) {
        u8* npc = static_cast<u8*>(AnimComp(p));
        u8* mat = At<u8*>(npc, kNpcMaterialAnim);
        const u8* facial = At<const u8*>(npc, kNpcFacial);
        index = 0;
        if (facial != nullptr && !((At<u8>(facial, kFacialFlags) >> 6) & 1)) {
            const u8* set = At<const u8*>(facial, kFacialSet);
            index = At<s32>(set, kFacialCount) + (At<s32>(set, kFacialExtraA) != 0) + (At<s32>(set, kFacialExtraB) != 0);
        }
        if (mat == nullptr || index < 0 || index >= At<s32>(mat, kMatAnimSlotCount)) return nullptr;
        return At<void**>(mat, kMatAnimSlots)[index];
    }

    // The look's tunic: "clothes" on its slot (again when something else took the slot), held at the tunic's frame.
    void SetTunic(Puppet& p) {
        int tunic = std::clamp(p.look.tunic, AP::GhostNet::TUNIC_GREEN, AP::GhostNet::TUNIC_BLUE);
        int index;
        void* slot = TunicSlot(p, index);
        if (slot == nullptr || p.tunic == kNoTunic) return;
        void* playing = At<void*>(slot, kSlotAnim);
        if (p.tunicAnim == nullptr || playing != p.tunicAnim) {
            SharedPtr out{};
            StrView name{"clothes", 7};
            Game::NpcComponent_PlayMaterialAnimation(&out, AnimComp(p), &name, true);
            Game::ReleaseOwner(out.ctrl);
            p.tunicAnim = At<void*>(slot, kSlotAnim);
            // not put there (the model has no "clothes"): no tunic for this model
            p.tunic = p.tunicAnim == nullptr || p.tunicAnim == playing ? kNoTunic : -1;
            if (p.tunic == kNoTunic) return;
        }
        if (tunic == p.tunic) return;
        Game::CallVirtual<void(float)>(slot, kSlotSetRate, 0);
        Game::CallVirtual<void(float)>(slot, kSlotSetFrame, kTunicFrames[tunic]);
        p.tunic = tunic;
    }

    // The sender's face on the face's slots, written again whenever a slot's frame differs; the puppet draws its own
    // face shapes while it differs from the instanced face (all frames 0). A model without all three slots (an older
    // mod's row) keeps the instanced face; a slot not found is looked for again each frame.
    void SetFace(Puppet& p) {
        bool all = true;
        for (int k = 0; k < kFaceParts; k++) {
            if (p.faceSlots[k] < 0) p.faceSlots[k] = MatSlotOf(p, kFaceAnims[k]);
            all &= p.faceSlots[k] >= 0;
        }
        int frames[kFaceParts] = {p.look.eye, p.look.mouth, p.look.ear};
        for (int k = 0; k < kFaceParts; k++)
            if (frames[k] < 0 || frames[k] >= kFaceFrames[k]) frames[k] = 0;
        p.ownFace = all && (frames[0] | frames[1] | frames[2]) != 0;
        if (!all) return;
        u8* mat = At<u8*>(AnimComp(p), kNpcMaterialAnim);
        u8** slots = At<u8**>(mat, kMatAnimSlots);
        for (int k = 0; k < kFaceParts; k++) {
            if (p.faceSlots[k] >= At<s32>(mat, kMatAnimSlotCount)) continue;
            u8* slot = slots[p.faceSlots[k]];
            if (frames[k] == p.faceSet[k] && At<float>(slot, kSlotFrame) == static_cast<float>(frames[k])) continue;
            Game::CallVirtual<void(float)>(slot, kSlotSetRate, 0);
            Game::CallVirtual<void(float)>(slot, kSlotSetFrame, static_cast<float>(frames[k]));
            p.faceSet[k] = frames[k];
        }
    }
}
