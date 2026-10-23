#pragma once
// g3d model objects and the model components that hold them (DESIGN.md, Showing received items, the NPC models;
// TESTING.md, Model object, Shape draw gate, Instanced models).
#include "game/actor.hpp"
#include "game/mem.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>
#include <string_view>

namespace Game {
    // A g3d resource's name (an FMDL's, FMAT's or FMAA's +0x8): a u16 length, then the characters; "" for null.
    inline std::string_view ResName(const u8* name) {
        return name != nullptr ? std::string_view(reinterpret_cast<const char*>(name + 2), Field<u16>(name, 0))
                               : std::string_view();
    }
    // A g3d resource's name from the resource; "" for null.
    inline std::string_view ResNameOf(const u8* res) { return ResName(At<const u8*>(res, 0x8)); }

    // A model's memory pool: its blocks' GPU addresses are gpu + their offsets.
    struct MemoryPool {
        u8 pad_0[0x8];
        void* cpu;                 // 0x8
        u8 pad_10[0x40];
        u64 gpu;                   // 0x50
    };
    static_assert(offsetof(MemoryPool, cpu) == 0x8 && offsetof(MemoryPool, gpu) == 0x50);

    struct ShapeObj {
        u8 pad_0[0x18];
        u64 blockOffset;           // 0x18: its block, in the model's pool
        u8 pad_20[0x50];
    };
    static_assert(offsetof(ShapeObj, blockOffset) == 0x18 && sizeof(ShapeObj) == 0x70);

    struct MaterialObj {
        const u8* res;             // FMAT
        u8 pad_8[0x18];
        u32 blockCopies;           // 0x20
        u8 pad_24[0x14];
        u64 blockOffset;           // 0x38: its shader parameter block, in the model's pool
        u8 pad_40[0x20];
        u64 blockSize;             // 0x60: the copies are this rounded up to 0x100 apart
        u8 pad_68[0x18];
    };
    static_assert(offsetof(MaterialObj, blockCopies) == 0x20 && offsetof(MaterialObj, blockOffset) == 0x38);
    static_assert(offsetof(MaterialObj, blockSize) == 0x60 && sizeof(MaterialObj) == 0x80);

    // A bone's local transform, computed into the world ones each frame.
    struct LocalTransform {
        u32 flags;                 // kLocalTranslateZero
        u8 pad_4[0x4c];
        float translate[3];        // 0x50
        u8 pad_5c[0x4];
    };
    static_assert(offsetof(LocalTransform, translate) == 0x50 && sizeof(LocalTransform) == 0x60);
    constexpr u32 kLocalTranslateZero = 1u << 27;

    struct Skeleton {
        const u8* res;             // FSKL
        u8 pad_8[0x10];
        LocalTransform* locals;    // 0x18: per bone, bone 0 the root
        float (*world)[16];        // 0x20: per bone: X, Y, Z axes, translation
    };
    static_assert(offsetof(Skeleton, locals) == 0x18 && offsetof(Skeleton, world) == 0x20);

    // A g3d model object.
    struct ModelObj {
        const u8* res;             // FMDL
        u8 pad_8[0x8];
        u32* materialVisible;      // 0x10: a bit per material
        u8 pad_18[0x18];
        u16 shapeCount;            // 0x30
        u16 materialCount;         // 0x32
        u8 pad_34[0x4];
        Skeleton* skeleton;        // 0x38
        ShapeObj* shapes;          // 0x40
        MaterialObj* materials;    // 0x48
        u8 pad_50[0x18];
        MemoryPool* pool;          // 0x68
    };
    static_assert(offsetof(ModelObj, materialVisible) == 0x10 && offsetof(ModelObj, shapeCount) == 0x30);
    static_assert(offsetof(ModelObj, materialCount) == 0x32 && offsetof(ModelObj, skeleton) == 0x38);
    static_assert(offsetof(ModelObj, shapes) == 0x40 && offsetof(ModelObj, materials) == 0x48);
    static_assert(offsetof(ModelObj, pool) == 0x68);

    // What a model instance's holder points at (the name is a guess).
    struct ModelHolder {
        u8 pad_0[0x10];
        ModelObj* model;           // 0x10
    };
    static_assert(offsetof(ModelHolder, model) == 0x10);

    // An instanced model's instance: position, rotation quaternion, scale.
    struct InstanceRecord {
        float pos[4], rot[4], scale[3], pad;
    };
    static_assert(sizeof(InstanceRecord) == 0x30);

    // A model instance. A shape draws only with its shapeVisible byte, its material's visibility bit and its bone's
    // all set.
    struct ModelInstance {
        u8 pad_0[0x60];
        ModelHolder* holder;       // 0x60
        u8 pad_68[0x378 - 0x68];
        u8* shapeVisible;          // 0x378: one byte per shape, 1 drawn
        u8 pad_380[0x18];
        Vector<InstanceRecord> records;  // 0x398: an instanced model's, turned into a storage buffer at each draw

        ModelObj* Model() const { return holder != nullptr ? holder->model : nullptr; }
    };
    static_assert(offsetof(ModelInstance, holder) == 0x60 && offsetof(ModelInstance, shapeVisible) == 0x378);
    static_assert(offsetof(ModelInstance, records) == 0x398);

    // A skeletal model component (vtable 0x1c4a060), a node.
    struct SkeletalModelComponent {
        Node node;
        u8 pad_e8[0x148 - sizeof(Node)];
        ModelInstance* instance;   // 0x148
        u8 pad_150[0x18];
        void* modelResource[3];    // 0x168: {resource, control block, model}
    };
    static_assert(sizeof(Node) == 0xe8);
    static_assert(offsetof(SkeletalModelComponent, instance) == 0x148);
    static_assert(offsetof(SkeletalModelComponent, modelResource) == 0x168);

    // An instanced model component (0x310 bytes, InstancedModel_Ctor): the game's for lawn grass and floor tiles.
    struct InstancedModelComponent {
        Entity entity;
        u8 pad_58[0x148 - 0x58];
        ModelInstance* instance;   // 0x148
        u8 pad_150[0x2f9 - 0x150];
        u8 noPush;                 // 0x2f9: set, with push clear, the game adds no instances
        u8 push;                   // 0x2fa
        u8 pad_2fb[0x310 - 0x2fb];
    };
    static_assert(offsetof(InstancedModelComponent, instance) == 0x148);
    static_assert(offsetof(InstancedModelComponent, noPush) == 0x2f9);
    static_assert(offsetof(InstancedModelComponent, push) == 0x2fa);
    static_assert(sizeof(InstancedModelComponent) == 0x310);

    // A skeletal model component's root bone moves by -center, putting the model's middle (model units) on the
    // component's origin: an NPC model stands on its feet there. False until the skeleton exists.
    inline bool MoveRootBone(SkeletalModelComponent* component, const Vec3& center) {
        ModelObj* model = component->instance != nullptr ? component->instance->Model() : nullptr;
        LocalTransform* root = model != nullptr && model->skeleton != nullptr ? model->skeleton->locals : nullptr;
        if (root == nullptr) return false;
        root->flags &= ~kLocalTranslateZero;
        root->translate[0] -= center.x;
        root->translate[1] -= center.y;
        root->translate[2] -= center.z;
        return true;
    }
}
