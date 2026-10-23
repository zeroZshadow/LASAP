#pragma once
// Model components (DESIGN.md, Showing received items, the NPC models).
#include "game/actor.hpp"
#include "game/mem.hpp"
#include "lib.hpp"
#include <string_view>

namespace Game {
    // A g3d resource's name (an FMDL's, FMAT's or FMAA's +0x8): a u16 length, then the characters; "" for null.
    inline std::string_view ResName(const u8* name) {
        return name != nullptr ? std::string_view(reinterpret_cast<const char*>(name + 2), Field<u16>(name, 0))
                               : std::string_view();
    }

    // A model component's root bone moves by -center, putting the model's middle (model units) on the component's
    // origin: an NPC model stands on its feet there. Component +0x148 the model instance, +0x60 -> +0x10 its model
    // object, +0x38 the skeleton, +0x18 its bones' local transforms (0x60 each, computed into the world ones each
    // frame; bone 0 the root): +0x0 flags (bit 27: translation zero), +0x50 translation. False until the skeleton
    // exists.
    inline bool MoveRootBone(u8* component, const Vec3& center) {
        constexpr size_t kModelInstance = 0x148, kInstanceObjects = 0x60, kObjectsModel = 0x10, kModelSkeleton = 0x38;
        constexpr size_t kSkeletonLocal = 0x18, kLocalFlags = 0x0, kLocalTranslate = 0x50;
        constexpr u32 kLocalTranslateZero = 0x08000000;
        u8* instance = At<u8*>(component, kModelInstance);
        u8* skeleton = At<u8*>(At<u8*>(At<u8*>(instance, kInstanceObjects), kObjectsModel), kModelSkeleton);
        u8* root = At<u8*>(skeleton, kSkeletonLocal);
        if (root == nullptr) return false;
        Field<u32>(root, kLocalFlags) &= ~kLocalTranslateZero;
        float* t = &Field<float>(root, kLocalTranslate);
        t[0] -= center.x;
        t[1] -= center.y;
        t[2] -= center.z;
        return true;
    }
}
