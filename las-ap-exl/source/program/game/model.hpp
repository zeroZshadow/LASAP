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

    // A model's memory pool (nn::gfx TMemoryPool<NVN>, MemoryPool_Initialize; 0x120 bytes): its blocks' GPU
    // addresses are gpu + their offsets (gpu is inside the NVN pool: measured, not traced).
    struct MemoryPool {
        void* nvnPool;             // nvnStorage's
        void* cpu;                 // 0x8: the memory the pool is made on
        u8 nvnStorage[0x40];       // 0x10: NVNmemoryPool, to 0x110
        u64 gpu;                   // 0x50
        u8 nvnStorage_58[0xb8];    // 0x58
        u8 state;                  // 0x110: 1 initialized
        u8 flags;                  // 0x111
        u8 pad_112[0xe];
    };
    static_assert(offsetof(MemoryPool, cpu) == 0x8 && offsetof(MemoryPool, gpu) == 0x50);
    static_assert(offsetof(MemoryPool, state) == 0x110 && sizeof(MemoryPool) == 0x120);

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
        u8 dirtyFlags;             // 0x2c: bit 0 a parameter is dirty
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
        u8 pad_78[0x8];            // no access found
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

    // nn::g3d ModelObj (g3d_ModelObj_Initialize; 0x90 bytes).
    struct ModelObj {
        const u8* res;             // FMDL
        u32* boneVisible;          // 0x8: a bit per bone
        u32* materialVisible;      // 0x10: a bit per material
        u8 viewCount;              // 0x18
        u8 hasBillboardShape;      // 0x19
        u16 flags;                 // 0x1a: bit 0 block buffer set up
        u8 pad_1c[0x4];
        void* field_20;            // 0x20: zeroed when made, no access found
        void* field_28;            // 0x28: the same
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
        u32 lodCount;              // 0x8c: the most meshes of a shape
    };
    static_assert(offsetof(ModelObj, materialVisible) == 0x10 && offsetof(ModelObj, shapeCount) == 0x30);
    static_assert(offsetof(ModelObj, materialCount) == 0x32 && offsetof(ModelObj, skeleton) == 0x38);
    static_assert(offsetof(ModelObj, shapes) == 0x40 && offsetof(ModelObj, materials) == 0x48);
    static_assert(offsetof(ModelObj, pool) == 0x68 && offsetof(ModelObj, lodCount) == 0x8c);
    static_assert(sizeof(ModelObj) == 0x90);

    // How a shape draws (from its material): the render layer and the render passes it takes part in. The passes have
    // no names in the binary; they are named by what selects them.
    struct ShapeRenderInfo {
        u32 materialIndex;
        u32 renderLayer;           // 0x4: bits 0-1 the pass filter; above 1 drawn back to front
        u8 field_8[0x20];          // 0x8: from the material
        u8 castShadow;             // 0x28: with ModelInstance::castShadow, pass 1 (likely the shadow map)
        u8 optionEnable;           // 0x29
        u8 mainPasses;             // 0x2a: passes 11 to 15, and the third shape binder
        u8 passFlag2b;             // 0x2b: passes 16, 17
        u8 water;                  // 0x2c: pass 10 and the water shape binder
        u8 flag_2d;                // 0x2d: not read
        u8 pad_2e[0x2];
        s32 depthBias;             // 0x30
        u32 cullMode;              // 0x34: 3 the pass's own
    };
    static_assert(offsetof(ShapeRenderInfo, castShadow) == 0x28 && offsetof(ShapeRenderInfo, depthBias) == 0x30);
    static_assert(sizeof(ShapeRenderInfo) == 0x38);

    // The game's model object (made by ModelData_Get; 0xd8 bytes): the g3d model and how its shapes draw.
    struct Model {
        ModelObj obj;
        u64 blockOffset;           // 0x90: its block buffer, in the graphics system's pool
        u32 blockSize;             // 0x98
        u32 bufferSize;            // 0x9c
        SharedPtr resource;        // 0xa0
        const u8* resModel;        // 0xb0: FMDL
        Vector<ShapeRenderInfo> shapeInfos;  // 0xb8: per shape
        u8 initialized;            // 0xd0
        u8 pad_d1[0x7];
    };
    static_assert(offsetof(Model, blockOffset) == 0x90 && offsetof(Model, shapeInfos) == 0xb8);
    static_assert(sizeof(Model) == 0xd8);

    // A shape's shader selectors, one per shape binder (the third may be null).
    struct ShapeShaders {
        void* shaders[3];
    };
    static_assert(sizeof(ShapeShaders) == 0x18);

    // A model's shared data (ModelData_Get, vtable 0x1c45aa8; 0x68 bytes), refcounted. ModelData_Get makes one per
    // call and lists it in ModelDataRegistry_ptr.
    struct ModelData {
        const uintptr_t* vtable;
        s32 refCount;              // 0x8
        u8 pad_c[0x4];
        Model* model;              // 0x10: a shared_ptr
        void* modelCtrl;           // 0x18
        s32 vertexStateCount;      // 0x20: the shape count
        u8 pad_24[0x4];
        Vector<ShapeShaders> shapeShaders;  // 0x28: per shape, owned
        s32 bufferCount;           // 0x40: the model's buffering, 2
        u8 pad_44[0x4];
        void** vertexStates;       // 0x48: per shape, nn::gfx VertexState
        SharedPtr resource;        // 0x50
        const u8* resModel;        // 0x60: FMDL
    };
    static_assert(offsetof(ModelData, model) == 0x10 && offsetof(ModelData, shapeShaders) == 0x28);
    static_assert(offsetof(ModelData, vertexStates) == 0x48 && offsetof(ModelData, resource) == 0x50);
    static_assert(sizeof(ModelData) == 0x68);

    // A named uniform block (UniformBlock_vtable 0x1c350a8, UniformBlock_Init; 0x48 bytes): count * 16 words,
    // uploaded per buffer.
    struct UniformBlock {
        const uintptr_t* vtable;
        u8 initialized;            // 0x8
        u8 pad_9[0x3];
        s32 wordCount;             // 0xc
        s32 count;                 // 0x10
        u8 pad_14[0x4];
        u32* words;                // 0x18
        void (*wordsDeleter)(u32*);  // 0x20
        void* gpuBuffers;          // 0x28: refcounted, 0x48 per buffer from +0x18
        String name;               // 0x30
    };
    static_assert(offsetof(UniformBlock, words) == 0x18 && offsetof(UniformBlock, name) == 0x30);
    static_assert(sizeof(UniformBlock) == 0x48);

    // A shape's shader and uniform binder (ShapeBinder_Ctor, ShapeBinder_vtable 0x1c47250; 0xa0 bytes). The water,
    // first and second kinds (WaterShapeBinder_vtable, ShapeBinder1_vtable, ShapeBinder2_vtable) bind more textures.
    struct ShapeBinder {
        const uintptr_t* vtable;
        Vector<void*> blocks;      // 0x8
        Vector<UniformBlock*> uniformBlocks;  // 0x20: bound by name
        void** vertexStates;       // 0x38: the model data's
        u32 uploadedMask;          // 0x40: a bit per buffer
        u8 pad_44[0x4];
        Vector<void*> blockBuffers;  // 0x48: per buffer, 0x800 bytes
        u8 samplerTableBuilt;      // 0x60
        u8 pad_61[0x7];
        Vector<u64> samplerTable;  // 0x68: per program sampler
        s32 envSpecLocation;       // 0x80: _envSpec0's, -1 until looked up
        s32 envLocalSpecLocation;  // 0x84: _envLocalSpec0's
        Vector<u8> blockLocations; // 0x88: {program, block, location} 0x18 each, a cache
    };
    static_assert(offsetof(ShapeBinder, vertexStates) == 0x38 && offsetof(ShapeBinder, blockBuffers) == 0x48);
    static_assert(offsetof(ShapeBinder, envSpecLocation) == 0x80 && sizeof(ShapeBinder) == 0xa0);

    struct ModelInstance;
    // A model instance's nn::g3d calculate-world callback (ModelInstance_BoneCallback_vtable 0x1c47728): emits the
    // bone's signal of the instance's boneSignals.
    struct BoneCallback {
        const uintptr_t* vtable;
        OrderedMap* signals;       // 0x8: the instance's boneSignals
        ModelInstance* owner;      // 0x10
    };
    static_assert(sizeof(BoneCallback) == 0x18);

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
        Vector<ShapeBinder*> binders[3];  // 0xc0: per shape, owned (ModelInstance_MakeShapeBinders)
        ShapeBinder waterBinder;   // 0x108: a WaterShapeBinder, only made and destroyed
        Vector<u32> lightIndices;  // 0x1a8: up to 16, copied into lightIndexList
        UniformBlock* lightIndexList;  // 0x1c0: owned ("LightIndexList")
        void (*lightIndexListDeleter)(UniformBlock*);  // 0x1c8
        Vec4 userParams[9];        // 0x1d0: copied into every shape block's user area each frame
        float userParam9;          // 0x260
        u8 initialized;            // 0x264
        s8 bufferIndex;            // 0x265
        u8 cullEnabled;            // 0x266
        u8 shapeCullMode;          // 0x267: 0 none, 1 per shape, 2 per mesh; the instanced draw culls records at 0
        u8 castShadow;             // 0x268: with ShapeRenderInfo::castShadow, pass 1 (likely the shadow map)
        u8 shapeOptionEnable;      // 0x269: a shader option of every shape
        u8 passFlagA;              // 0x26a: passes 41, 42
        u8 passFlagB;              // 0x26b: passes 18, 44
        u8 translucentPassFlag;    // 0x26c: render layer 2's pass 22 instead of 20, 21
        u8 skeletonDirty;          // 0x26d
        u8 transformDirty;         // 0x26e
        u8 shaderOption26f;        // 0x26f: a shader option, set by the model components' setups
        u32 shaderDirtyMask;       // 0x270: a bit per buffer whose shader options are to push
        float depthBias;           // 0x274: added to every draw's
        u8 forcePositionUpdate;    // 0x278
        u8 culled;                 // 0x279
        u8 pad_27a[0x2];
        u32 envIndex;              // 0x27c: a shader option, from the renderer's environment
        Signal changed;            // 0x280
        OrderedMap boneSignals;    // 0x2d8: bone index to an owned Signal (ModelInstance_ConnectBoneCallback)
        BoneCallback boneCallback; // 0x2f0: the skeleton's calcWorldCallback while boneSignals has any
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

        ModelObj* Model() const { return data != nullptr && data->model != nullptr ? &data->model->obj : nullptr; }
    };
    static_assert(offsetof(ModelInstance, data) == 0x60 && offsetof(ModelInstance, position) == 0x80);
    static_assert(offsetof(ModelInstance, binders) == 0xc0 && offsetof(ModelInstance, waterBinder) == 0x108);
    static_assert(offsetof(ModelInstance, lightIndices) == 0x1a8 && offsetof(ModelInstance, lightIndexList) == 0x1c0);
    static_assert(offsetof(ModelInstance, userParams) == 0x1d0 && offsetof(ModelInstance, shapeCullMode) == 0x267);
    static_assert(offsetof(ModelInstance, skeletonDirty) == 0x26d && offsetof(ModelInstance, shaderDirtyMask) == 0x270);
    static_assert(offsetof(ModelInstance, depthBias) == 0x274 && offsetof(ModelInstance, culled) == 0x279);
    static_assert(offsetof(ModelInstance, envIndex) == 0x27c && offsetof(ModelInstance, changed) == 0x280);
    static_assert(offsetof(ModelInstance, boneSignals) == 0x2d8 && offsetof(ModelInstance, boneCallback) == 0x2f0);
    static_assert(offsetof(ModelInstance, calcOverride) == 0x310);
    static_assert(offsetof(ModelInstance, renderMode) == 0x370 && offsetof(ModelInstance, shapeVisible) == 0x378);
    static_assert(offsetof(ModelInstance, records) == 0x398 && sizeof(ModelInstance) == 0x3c0);
    namespace ModelInstanceVirtual {
        constexpr size_t kCalc = 0x88, kDraw = 0x90, kInstanceCount = 0x98, kCalcModel = 0xc8;
    }

    // nn::gfx BlendState<NVN> (BlendState_Initialize).
    struct BlendState {
        void* nvnBlendStates;      // targetCount NVNblendState
        u8 nvnColorState[4];       // 0x8
        u8 nvnChannelMaskState[4]; // 0xc
        float blendConstant[4];    // 0x10
        u32 memorySize;            // 0x20
        u8 state;                  // 0x24: 1 initialized
        u8 targetCount;            // 0x25
        u8 pad_26[0x2];
    };
    static_assert(offsetof(BlendState, blendConstant) == 0x10 && sizeof(BlendState) == 0x28);

    // nn::gfx BlendTargetStateInfo.
    struct BlendTargetStateInfo {
        u8 flags;                  // bit 0 blend enabled
        u8 srcColor;               // 0x1
        u8 dstColor;               // 0x2
        u8 colorOp;                // 0x3
        u8 srcAlpha;               // 0x4
        u8 dstAlpha;               // 0x5
        u8 alphaOp;                // 0x6
        u8 channelMask;            // 0x7
        u8 reserved[0x18];         // 0x8
    };
    static_assert(sizeof(BlendTargetStateInfo) == 0x20);

    // nn::gfx BlendStateInfo.
    struct BlendStateInfo {
        u8 targetCount;
        u8 logicOperation;         // 0x1
        u16 flags;                 // 0x2: bit 2 independent blend, 3 logic operation
        float blendConstant[4];    // 0x4
        u8 pad_14[0x4];
        const BlendTargetStateInfo* targets;  // 0x18
        u8 reserved[0x10];         // 0x20
    };
    static_assert(offsetof(BlendStateInfo, targets) == 0x18 && sizeof(BlendStateInfo) == 0x30);

    // A model component's blend state: the nn::gfx state, the info it is built from and whether it is built.
    struct BlendStateObj {
        BlendState state;
        BlendStateInfo info;       // 0x28: its targets point into SkeletalModel_Setup's stack (stale)
        BlendTargetStateInfo target;  // 0x58
        u8 built;                  // 0x78
        u8 pad_79[0x7];
    };
    static_assert(offsetof(BlendStateObj, target) == 0x58 && sizeof(BlendStateObj) == 0x80);

    // nn::gfx DepthStencilState<NVN>.
    struct DepthStencilState {
        u8 nvnState[8];            // NVNdepthStencilState
        u32 stencilValueMask;      // 0x8
        u32 stencilMask;           // 0xc
        u32 backRef;               // 0x10
        u32 frontRef;              // 0x14
        u8 state;                  // 0x18
        u8 pad_19;
        u16 flags;                 // 0x1a
    };
    static_assert(offsetof(DepthStencilState, state) == 0x18 && sizeof(DepthStencilState) == 0x1c);

    // nn::gfx StencilStateInfo.
    struct StencilStateInfo {
        u8 sFail;
        u8 dFail;                  // 0x1
        u8 dPass;                  // 0x2
        u8 func;                   // 0x3
        u8 ref;                    // 0x4
        u8 reserved[0xb];          // 0x5
    };
    static_assert(sizeof(StencilStateInfo) == 0x10);

    // nn::gfx DepthStencilStateInfo.
    struct DepthStencilStateInfo {
        u8 depthFunc;
        u8 stencilReadMask;        // 0x1
        u8 stencilWriteMask;       // 0x2
        u8 pad_3;
        u16 flags;                 // 0x4: bit 0 depth test, 1 depth write, 2 stencil test
        u8 pad_6[0x2];
        StencilStateInfo front;    // 0x8
        StencilStateInfo back;     // 0x18
        u8 reserved[0x18];         // 0x28
    };
    static_assert(offsetof(DepthStencilStateInfo, front) == 0x8 && sizeof(DepthStencilStateInfo) == 0x40);

    // A model component's depth stencil state.
    struct DepthStencilStateObj {
        DepthStencilState state;
        DepthStencilStateInfo info;  // 0x1c
        u8 built;                  // 0x5c
        u8 pad_5d[0x3];
    };
    static_assert(offsetof(DepthStencilStateObj, built) == 0x5c && sizeof(DepthStencilStateObj) == 0x60);

    // nn::gfx RasterizerState<NVN>.
    struct RasterizerState {
        u8 nvnPolygonState[4];
        u8 nvnMultisampleState[0x18];  // 0x4
        u32 sampleMask;            // 0x1c
        float depthBias;           // 0x20
        float depthBiasClamp;      // 0x24
        float slopeScaledDepthBias;  // 0x28
        u8 state;                  // 0x2c
        u8 flags;                  // 0x2d: bit 0 multisample, 1 depth clip, 2 raster enabled, 3 conservative
        u8 pad_2e[0x2];
    };
    static_assert(offsetof(RasterizerState, depthBias) == 0x20 && sizeof(RasterizerState) == 0x30);

    // nn::gfx RasterizerStateInfo.
    struct RasterizerStateInfo {
        u8 fillMode;
        u8 frontFace;              // 0x1
        u8 cullMode;               // 0x2
        u8 topology;               // 0x3
        u16 flags;                 // 0x4: bit 0 raster disabled, 1 multisample, 2 depth clip disabled, 3 scissor
        u8 conservativeMode;       // 0x6
        u8 pad_7;
        float slopeScaledDepthBias;  // 0x8
        s32 depthBias;             // 0xc
        float depthBiasClamp;      // 0x10
        u8 pad_14[0x4];
        u8 sampleCount;            // 0x18
        u8 pad_19;
        u8 alphaToCoverage;        // 0x1a
        u8 pad_1b;
        u32 sampleMask;            // 0x1c
        u8 reserved[0x20];         // 0x20
    };
    static_assert(offsetof(RasterizerStateInfo, sampleCount) == 0x18 && sizeof(RasterizerStateInfo) == 0x40);

    // A model component's rasterizer state.
    struct RasterizerStateObj {
        RasterizerState state;
        RasterizerStateInfo info;  // 0x30
        u8 built;                  // 0x70
        u8 pad_71[0x3];
    };
    static_assert(offsetof(RasterizerStateObj, built) == 0x70 && sizeof(RasterizerStateObj) == 0x74);

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
        BlendStateObj blend;       // 0x198
        DepthStencilStateObj depthStencil;  // 0x218
        RasterizerStateObj rasterizer;  // 0x278
        u8 worldDirty;             // 0x2ec: the node's world goes to the instance
        u8 pad_2ed[0x3];
        u32 signalConnection;      // 0x2f0: on node.transformChanged
        u32 order;                 // 0x2f4: 0x401
        u8 instanceShared;         // 0x2f8: the instance existed at setup
        u8 pad_2f9[0x7];
    };
    static_assert(offsetof(SkeletalModelComponent, instance) == 0x148);
    static_assert(offsetof(SkeletalModelComponent, modelResource) == 0x168);
    static_assert(offsetof(SkeletalModelComponent, blend) == 0x198);
    static_assert(offsetof(SkeletalModelComponent, depthStencil) == 0x218);
    static_assert(offsetof(SkeletalModelComponent, rasterizer) == 0x278);
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
