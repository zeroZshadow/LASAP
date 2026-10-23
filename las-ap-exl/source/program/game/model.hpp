#pragma once
// nn::g3d model objects, the game's model instances and the model components that hold them (scripts/re/MODELS.md;
// DESIGN.md, Showing received items; TESTING.md, Model object, Shape draw gate, Instanced models).
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

    // A model's memory pool (nn::gfx): its blocks' GPU addresses are gpu + their offsets. Measured, not traced.
    struct MemoryPool {
        u8 pad_0[0x8];
        void* cpu;                 // 0x8
        u8 pad_10[0x40];
        u64 gpu;                   // 0x50
    };
    static_assert(offsetof(MemoryPool, cpu) == 0x8 && offsetof(MemoryPool, gpu) == 0x50);

    // nn::g3d ShapeObj (0x70).
    struct ShapeObj {
        const u8* res;             // ResShape
        u32 flags;                 // 0x8: bit 0 block buffer valid, bit 1 blend shape dirty
        u8 viewCount;              // 0xc
        u8 viewDependent;          // 0xd
        u8 shapeBlockCount;        // 0xe
        u8 bufferCount;            // 0xf
        MemoryPool* pool;          // 0x10
        u64 blockOffset;           // 0x18: its shape block, in the pool
        void* shapeBlocks;         // 0x20: gfx buffers, 0x48 each
        float* blendWeights;       // 0x28: per key shape
        u32* blendWeightValid;     // 0x30
        void* boundings;           // 0x38: 0x40 per mesh; the draws cull against them
        void* buffer_40;           // 0x40
        void** keyShapeBuffers;    // 0x48
        void* userArea;            // 0x50: copied into the shape block
        u64 userAreaSize;          // 0x58
        void* userPtr;             // 0x60
        void* buffer;              // 0x68
    };
    static_assert(offsetof(ShapeObj, pool) == 0x10 && offsetof(ShapeObj, blockOffset) == 0x18);
    static_assert(offsetof(ShapeObj, userArea) == 0x50 && sizeof(ShapeObj) == 0x70);

    // nn::g3d MaterialObj (0x80).
    struct MaterialObj {
        const u8* res;             // FMAT
        u16 flags;                 // 0x8: bit 0 block buffer valid
        u8 bufferCount;            // 0xa
        u8 pad_b[0x5];
        u32* paramDirty;           // 0x10
        u32* paramDirtyPerBuffer;  // 0x18
        u32 blockCopies;           // 0x20: the dirty set's buffering count
        s32 dirtyWordCount;        // 0x24
        s32 paramCount;            // 0x28
        u8 field_2c;               // 0x2c
        u8 dirtyBufferMask;        // 0x2d
        u8 pad_2e[0x2];
        MemoryPool* pool;          // 0x30
        u64 blockOffset;           // 0x38: its shader parameter block, in the pool
        void* blocks;              // 0x40: gfx buffers, 0x48 each
        void* srcParams;           // 0x48: a CPU copy of the FMAT's
        void** textures;           // 0x50
        void** samplers;           // 0x58
        u64 blockSize;             // 0x60: the copies are this rounded up to 0x100 apart
        void* userPtr;             // 0x68
        void* buffer;              // 0x70
        u8 pad_78[0x8];
    };
    static_assert(offsetof(MaterialObj, blockCopies) == 0x20 && offsetof(MaterialObj, pool) == 0x30);
    static_assert(offsetof(MaterialObj, blockOffset) == 0x38 && offsetof(MaterialObj, srcParams) == 0x48);
    static_assert(offsetof(MaterialObj, blockSize) == 0x60 && sizeof(MaterialObj) == 0x80);

    // nn::g3d LocalMtx: a bone's local transform, computed into the world ones each frame.
    struct LocalTransform {
        u32 flags;                 // kLocalTranslateZero, the ResBone's
        u8 pad_4[0xc];
        float scale[4];            // 0x10
        float rotation[3][4];      // 0x20: X, Y, Z axes
        float translate[3];        // 0x50
        u8 pad_5c[0x4];
    };
    static_assert(offsetof(LocalTransform, scale) == 0x10 && offsetof(LocalTransform, rotation) == 0x20);
    static_assert(offsetof(LocalTransform, translate) == 0x50 && sizeof(LocalTransform) == 0x60);
    constexpr u32 kLocalTranslateZero = 1u << 27;

    // nn::g3d SkeletonObj (0x80).
    struct Skeleton {
        const u8* res;             // FSKL
        u16 flags;                 // 0x8
        u8 bufferCount;            // 0xa
        u8 pad_b[0x5];
        const u8* bones;           // 0x10: ResBone, 0x60 each
        LocalTransform* locals;    // 0x18: per bone, bone 0 the root
        float (*world)[16];        // 0x20: per bone: X, Y, Z axes, translation
        void* scales;              // 0x28
        MemoryPool* pool;          // 0x30
        u64 poolOffset;            // 0x38
        void* paletteBlocks;       // 0x40: the skinning matrices, a gfx buffer per buffer
        u64 paletteBlockSize;      // 0x48
        u16 boneCount;             // 0x50
        u16 callbackBone;          // 0x52: 0xffff none
        u8 pad_54[0x4];
        void* calcWorldCallback;   // 0x58
        LocalTransform* localsOriginal;  // 0x60
        void* worldOriginal;       // 0x68
        void* userPtr;             // 0x70
        void* buffer;              // 0x78
    };
    static_assert(offsetof(Skeleton, locals) == 0x18 && offsetof(Skeleton, world) == 0x20);
    static_assert(offsetof(Skeleton, boneCount) == 0x50 && sizeof(Skeleton) == 0x80);

    // nn::g3d ModelObj (g3d_ModelObj_Initialize).
    struct ModelObj {
        const u8* res;             // FMDL
        u32* boneVisible;          // 0x8: a bit per bone
        u32* materialVisible;      // 0x10: a bit per material
        u8 viewCount;              // 0x18
        u8 hasBillboardShape;      // 0x19
        u16 flags;                 // 0x1a: bit 0 block buffer set up
        u8 pad_1c[0x14];
        u16 shapeCount;            // 0x30
        u16 materialCount;         // 0x32
        u8 pad_34[0x4];
        Skeleton* skeleton;        // 0x38
        ShapeObj* shapes;          // 0x40
        MaterialObj* materials;    // 0x48
        void* bounding;            // 0x50
        void* userPtr;             // 0x58
        void* buffer;              // 0x60
        MemoryPool* pool;          // 0x68: the block buffers'
        u64 poolOffset;            // 0x70
        void* onBoneVisibilityChanged;      // 0x78: (model, bone)
        void* onMaterialVisibilityChanged;  // 0x80: (model, material)
        u8 forceRigidCalc;         // 0x88
        u8 pad_89[0x3];
        u32 field_8c;              // 0x8c
    };
    static_assert(offsetof(ModelObj, materialVisible) == 0x10 && offsetof(ModelObj, shapeCount) == 0x30);
    static_assert(offsetof(ModelObj, materialCount) == 0x32 && offsetof(ModelObj, skeleton) == 0x38);
    static_assert(offsetof(ModelObj, shapes) == 0x40 && offsetof(ModelObj, materials) == 0x48);
    static_assert(offsetof(ModelObj, pool) == 0x68 && offsetof(ModelObj, field_8c) == 0x8c);

    // A model's shared data (ModelData_Get, vtable 0x1c45aa8; 0x68 bytes), refcounted and cached per resource.
    struct ModelData {
        const uintptr_t* vtable;
        s32 refCount;              // 0x8
        u8 pad_c[0x4];
        ModelObj* model;           // 0x10
        u8 pad_18[0x8];
        s32 gfxObjectCount;        // 0x20
        u8 pad_24[0x4];
        Vector<u8> shaderSets;     // 0x28: three owned objects per element
        s32 mode;                  // 0x40
        u8 pad_44[0x4];
        void** gfxObjects;         // 0x48
        SharedPtr resource;        // 0x50
        u8 pad_60[0x8];
    };
    static_assert(offsetof(ModelData, model) == 0x10 && offsetof(ModelData, shaderSets) == 0x28);
    static_assert(offsetof(ModelData, resource) == 0x50 && sizeof(ModelData) == 0x68);

    // An instanced model's instance: position, rotation quaternion, scale.
    struct InstanceRecord {
        float pos[4], rot[4], scale[3], pad;
    };
    static_assert(sizeof(InstanceRecord) == 0x30);

    // A model instance (ModelInstance_Ctor, vtable 0x1c47640; 0x3a0 bytes): what the renderer draws. A shape draws
    // only with its shapeVisible byte, its material's visibility bit and its bone's all set. The instanced subclass
    // (vtable 0x1c34eb8, 0x3c0 bytes) adds instanceCount and records; a plain instance ends at 0x3a0.
    struct ModelInstance {
        const uintptr_t* vtable;
        WeakPtr self;              // 0x8
        u8 pad_18[0x8];
        u64 passMask;              // 0x20
        Vector<u32> passes;        // 0x28
        u64 layerMask;             // 0x40
        Vector<u8> layers;         // 0x48
        ModelData* data;           // 0x60: intrusive
        void* blendState;          // 0x68: the skeletal model component's states
        void* depthStencilState;   // 0x70
        void* rasterizerState;     // 0x78
        Vec4 position;             // 0x80: the model component's node world
        Vec4 rotation;             // 0x90
        Vec4 scale;                // 0xa0
        Vec4 lastPosition;         // 0xb0
        Vector<void*> owned[3];    // 0xc0
        u8 object_108[0xa0];       // 0x108: vtable 0x1c47e68
        Vector<u8> vector_1a8;     // 0x1a8
        void* object_1c0;          // 0x1c0: owned, with its deleter
        void* object_1c0Deleter;   // 0x1c8
        Vec4 userParams[9];        // 0x1d0: copied into every shape block's user area each frame
        float userParam9;          // 0x260
        u8 initialized;            // 0x264
        s8 bufferIndex;            // 0x265
        u8 cullEnabled;            // 0x266
        u8 field_267;              // 0x267: set, the instanced draw skips each record's bounds
        u8 field_268[0x5];         // 0x268
        u8 skeletonDirty;          // 0x26d
        u8 transformDirty;         // 0x26e
        u8 field_26f;              // 0x26f
        s32 field_270;             // 0x270
        u32 field_274;             // 0x274
        u8 forcePositionUpdate;    // 0x278
        u8 culled;                 // 0x279
        u8 pad_27a[0x6];
        u8 changed[0x58];          // 0x280: a signal
        u8 map_2d8[0x18];          // 0x2d8
        u8 object_2f0[0x18];       // 0x2f0: vtable 0x1c47728
        u8 pad_308[0x8];
        Function calcOverride;     // 0x310: Calc calls it instead of its virtual +0xc8 when set
        Function afterCalc;        // 0x340
        u8 renderMode;             // 0x370: 0 hidden, 1 normal
        u8 pad_371[0x7];
        Vector<u8> shapeVisible;   // 0x378: one byte per shape, 1 drawn
        u8 shadowPass;             // 0x390
        u8 pad_391[0x3];
        u32 instanceCount;         // 0x394: the instanced subclass's
        Vector<InstanceRecord> records;  // 0x398: the instanced subclass's, turned into a storage buffer at each draw
        void* infoArray;           // 0x3b0: the instanced subclass's
        void* infoArrayDeleter;    // 0x3b8

        ModelObj* Model() const { return data != nullptr ? data->model : nullptr; }
    };
    static_assert(offsetof(ModelInstance, data) == 0x60 && offsetof(ModelInstance, position) == 0x80);
    static_assert(offsetof(ModelInstance, object_108) == 0x108 && offsetof(ModelInstance, userParams) == 0x1d0);
    static_assert(offsetof(ModelInstance, skeletonDirty) == 0x26d && offsetof(ModelInstance, culled) == 0x279);
    static_assert(offsetof(ModelInstance, changed) == 0x280 && offsetof(ModelInstance, calcOverride) == 0x310);
    static_assert(offsetof(ModelInstance, renderMode) == 0x370 && offsetof(ModelInstance, shapeVisible) == 0x378);
    static_assert(offsetof(ModelInstance, records) == 0x398 && sizeof(ModelInstance) == 0x3c0);
    namespace ModelInstanceVirtual {
        constexpr size_t kCalc = 0x88, kDraw = 0x90, kInstanceCount = 0x98, kCalcModel = 0xc8;
    }

    // A skeletal model component (SkeletalModel_vtable 0x1c4a060; 0x300 bytes), a node: the model instance and the
    // render states it draws with.
    struct SkeletalModelComponent {
        Node node;
        ModelInstance* instance;   // 0x148: a shared_ptr
        void* instanceCtrl;        // 0x150
        WeakPtr modelList;         // 0x158: Framework::renderer
        void* modelResource[3];    // 0x168: {resource, control block, model}
        void* blendStatePtr;       // 0x180: points at blendState
        void* depthStencilStatePtr;  // 0x188
        void* rasterizerStatePtr;  // 0x190
        u8 blendState[0x28];       // 0x198: nn::gfx
        u8 blendInfo[0x50];        // 0x1c0
        u8 blendStateBuilt;        // 0x210
        u8 pad_211[0x7];
        u8 depthStencilState[0x1c];  // 0x218: nn::gfx
        u8 depthStencilInfo[0x40]; // 0x234
        u8 depthStencilStateBuilt; // 0x274
        u8 pad_275[0x3];
        u8 rasterizerState[0x30];  // 0x278: nn::gfx
        u8 rasterizerInfo[0x40];   // 0x2a8
        u8 rasterizerStateBuilt;   // 0x2e8
        u8 pad_2e9[0x3];
        u8 worldDirty;             // 0x2ec: the node's world goes to the instance
        u8 pad_2ed[0x3];
        u32 signalConnection;      // 0x2f0: on node.transformChanged
        u32 order;                 // 0x2f4: 0x401
        u8 instanceShared;         // 0x2f8: the instance existed at setup
        u8 pad_2f9[0x7];
    };
    static_assert(offsetof(SkeletalModelComponent, instance) == 0x148);
    static_assert(offsetof(SkeletalModelComponent, modelResource) == 0x168);
    static_assert(offsetof(SkeletalModelComponent, blendState) == 0x198);
    static_assert(offsetof(SkeletalModelComponent, depthStencilState) == 0x218);
    static_assert(offsetof(SkeletalModelComponent, rasterizerState) == 0x278);
    static_assert(offsetof(SkeletalModelComponent, worldDirty) == 0x2ec);
    static_assert(offsetof(SkeletalModelComponent, instanceShared) == 0x2f8 && sizeof(SkeletalModelComponent) == 0x300);
    namespace SkeletalModelVirtual {
        constexpr size_t kSetup = 0x20, kSetRenderMode = 0x80, kShow = 0x108, kHide = 0x110;
    }

    // An instanced model component (InstancedModel_Ctor, vtable 0x1c49340; 0x310 bytes): the game's for lawn grass
    // and floor tiles, a skeletal model component whose own fields begin in the base's tail padding. Its virtual +0x40
    // pushes the node's world as a record while visible and not added this frame.
    struct InstancedModelComponent {
        Node node;
        ModelInstance* instance;   // 0x148: the instanced subclass, a shared_ptr
        void* instanceCtrl;        // 0x150
        u8 skeletalModel[0x2f9 - 0x158];  // 0x158: the rest of the skeletal model component
        u8 added;                  // 0x2f9: a record was pushed this frame; a per-frame task clears it
        u8 visible;                // 0x2fa: records are pushed only while set
        u8 pad_2fb;
        s32 cacheKey;              // 0x2fc
        u32 shapeMask;             // 0x300: a bit per shape
        u8 pad_304[0xc];
    };
    static_assert(offsetof(InstancedModelComponent, instance) == 0x148);
    static_assert(offsetof(InstancedModelComponent, added) == 0x2f9);
    static_assert(offsetof(InstancedModelComponent, visible) == 0x2fa);
    static_assert(offsetof(InstancedModelComponent, shapeMask) == 0x300);
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
