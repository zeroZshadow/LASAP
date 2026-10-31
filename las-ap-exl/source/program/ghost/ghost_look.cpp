// The puppets' look (DESIGN.md, Ghosts): the materials and shapes a ghost draws, its tunic and its face.
#include "game/actors/npc.hpp"
#include "game/animation.hpp"
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
    // on a slot of the NPC's material animation component; the slot is the facial row's slot count unless the NPC
    // has no facial control or its destroy is requested, then 0 (NpcComponent_MakeMaterialAnimator counts the same).
    using Game::MatAnimSlot;
    namespace Slot = Game::MatAnimSlotVirtual;
    constexpr float kTunicFrames[] = {0, 1, 3};
    // The face: Link's eyes, mouth and ears are material animations held at a frame ("eye", "mouth", "ear"). The
    // APGhost row's facial row (Facial.gsheet, ap_player_ghost.py) names them, so the NPC's material animation
    // component has a slot for each (before the tunic's), at frame 0 with no blinking; the module finds each by its
    // animation's name and holds its frame.
    // The instanced face (Instanced models) draws as the model draws without these animations, which is frame 0 of
    // each; a ghost with another face draws its own face shapes, the frames set on its slots.
    constexpr const char* kFaceAnims[] = {"eye", "mouth", "ear"};
    constexpr int kFaceFrames[kFaceParts] = {14, 14, 2};  // the FMAAs' frames

    // The model's visibility words a ghost draws with: the materials it shows (kShownMaterials).
    void ComputeShownMats(Puppet& p) {
        u16 count = std::min<u16>(p.model->materialCount, 32 * 4);
        const Game::MaterialObj* mats = p.model->materials;
        std::fill(std::begin(p.shownMats), std::end(p.shownMats), 0);
        for (u16 i = 0; mats != nullptr && i < count; i++) {
            std::string_view name;
            name = Game::ResNameOf(mats[i].res);
            auto named = [&](const char* m) { return name == m; };
            if (std::any_of(std::begin(kShownMaterials), std::end(kShownMaterials), named))
                p.shownMats[i / 32] |= 1u << (i % 32);
        }
        p.matWords = (count + 31) / 32;
    }

    // The slot of the puppet's material animation component that holds the animation of that name, or -1.
    int MatSlotOf(const Puppet& p, std::string_view name) {
        Game::Actors::NpcComponent* npc = NpcOf(p);
        const Game::MaterialAnimComponent* mat = npc != nullptr ? npc->materialAnim : nullptr;
        if (mat == nullptr) return -1;
        for (int i = 0; mat->slots.first != nullptr && i < mat->slotCount; i++) {
            const Game::MaterialAnim* anim = mat->slots[i] != nullptr ? mat->slots[i]->anim : nullptr;
            if (anim != nullptr && anim->res != nullptr && Game::ResNameOf(anim->res) == name) return i;
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
        u16 count = std::min<u16>(p.model->materialCount, 32 * 4);
        u32* visible = p.model->materialVisible;
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
        Game::ModelInstance* instance = AnimatorInstance(AnimatorOf(p));
        if (instance == nullptr || instance->Model() != p.model || p.model == nullptr) return;
        u8* shapes = instance->shapeVisible.first;
        if (shapes == nullptr) return;
        int count = std::min<int>(p.model->shapeCount, AP::GhostNet::SHAPE_COUNT);
        u32 mask = p.detail == 2 ? g_ShapeMask : g_ShapeMask & g_CrowdMask;
        mask &= ~(kInstancedShapes & ~(p.ownFace ? kFaceShapes : 0));
        for (int i = 0; i < count; i++) shapes[i] = (p.look.shapes & mask) >> i & 1;
    }

    MatAnimSlot* TunicSlot(const Puppet& p, int& index) {
        Game::Actors::NpcComponent* npc = NpcOf(p);
        Game::MaterialAnimComponent* mat = npc != nullptr ? npc->materialAnim : nullptr;
        const Game::Actors::FacialControl* facial = npc != nullptr ? npc->facial : nullptr;
        index = 0;
        if (facial != nullptr && !facial->entity.flags.Has(Game::EntityFlag::DestroyRequested)) {
            const Game::Actors::FacialRow* row = facial->row;
            index = row != nullptr ? row->SlotCount() : 0;
        }
        if (mat == nullptr || index < 0 || index >= mat->slotCount) return nullptr;
        return mat->slots[index];
    }

    // The look's tunic: "clothes" on its slot (again when something else took the slot), held at the tunic's frame.
    void SetTunic(Puppet& p) {
        int tunic = std::clamp(p.look.tunic, AP::GhostNet::TUNIC_GREEN, AP::GhostNet::TUNIC_BLUE);
        int index;
        MatAnimSlot* slot = TunicSlot(p, index);
        if (slot == nullptr || p.tunic == kNoTunic) return;
        void* playing = slot->anim;
        if (p.tunicAnim == nullptr || playing != p.tunicAnim) {
            StrView name{"clothes", 7};
            Game::Shared<> played;
            Game::NpcComponent_PlayMaterialAnimation(played.Reset(), NpcOf(p), &name, true);
            played = {};
            p.tunicAnim = slot->anim;
            // not put there (the model has no "clothes"): no tunic for this model
            p.tunic = p.tunicAnim == nullptr || p.tunicAnim == playing ? kNoTunic : -1;
            if (p.tunic == kNoTunic) return;
        }
        if (tunic == p.tunic) return;
        Game::CallVirtual<void(float)>(slot, Slot::kSetRate, 0);
        Game::CallVirtual<void(float)>(slot, Slot::kSetFrame, kTunicFrames[tunic]);
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
        Game::Actors::NpcComponent* npc = NpcOf(p);
        Game::MaterialAnimComponent* mat = npc != nullptr ? npc->materialAnim : nullptr;
        if (mat == nullptr) return;
        for (int k = 0; k < kFaceParts; k++) {
            if (p.faceSlots[k] >= mat->slotCount) continue;
            MatAnimSlot* slot = mat->slots[p.faceSlots[k]];
            float frame = slot != nullptr ? slot->lastFrame : 0;
            if (frames[k] == p.faceSet[k] && frame == static_cast<float>(frames[k])) continue;
            Game::CallVirtual<void(float)>(slot, Slot::kSetRate, 0);
            Game::CallVirtual<void(float)>(slot, Slot::kSetFrame, static_cast<float>(frames[k]));
            p.faceSet[k] = frames[k];
        }
    }
}
