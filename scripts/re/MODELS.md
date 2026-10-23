# Models and animation (game 1.0.0)

The game's model objects (nn::g3d), the model instances the renderer draws, the model components that hold them and the animation components that pose them, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. Field names are those of `game/model.hpp` and `game/animation.hpp`, which list every field; this file gives the evidence and what their comments leave out. "Guess" marks what the code suggests but was not followed to the end. TESTING.md, Reverse engineering, has Link's model, his shapes, the model material's parameter block and the instanced draw; this file does not repeat them.

## nn::g3d objects

The layouts follow NX nn::g3d: ResShape's material index at `+0x52` and bone index at `+0x54`, LocalMtx's flag bit 27 (TRANS_ZERO), AnimFrameCtrl's play policy function pointers. Matrices are nn::util SIMD types (Matrix4x3f, 4 × float4 = 0x40 bytes).

| Object | Size | Initialize | |
|---|---|---|---|
| `ModelObj` | 0x90 | `g3d_ModelObj_Initialize` `0x1129a30 (model, arg, buffer, size)` | the first part of the game's `Model` (below). `lodCount` (`+0x8c`, InitializeArgument `+0x20`) is the most meshes of a shape (ResShape `+0x5b`, at least 1); `field_20` and `field_28` are zeroed and never accessed. Makes `shapes` (0x70 apart) and `materials` (0x80 apart), and the `skeleton` in the same buffer (0x80 bytes, zeroed) unless the argument passes one. `boneVisible` from ResBone `+0x34` bit 0; `hasBillboardShape` set when a shape's bone has billboard bits (ResBone `+0x34 & 0x70000`), read by the instance's view-dependent calculation. `g3d_ModelObj_SetupBlockBuffer` (`0x112a210`) writes `pool` and `poolOffset` and sets `flags` bit 0. The two visibility callbacks are called `(model, index)` when a bit changes. `forceRigidCalc` gives skinned shapes `CalculateShape` too (`0x112a7a0`, `0x1037760`) |
| `Skeleton` (SkeletonObj) | 0x80 | `g3d_SkeletonObj_Initialize` `0x1139ed0` | `res` the FSKL: `+0x10` ResBone array (0x60 each), `+0x18` matrix-to-bone s16 table, `+0x20` inverse bind matrices (3 × 4), `+0x38` bone count, `+0x3a` smooth count, `+0x3c` rigid count. `flags` from ResSkeleton `+0x4`; byte `+0x9 & 3` picks the world calculation (`g3d_ModelObj_CalculateWorld` `0x112a4b0`). `locals` and `world` equal `localsOriginal` and `worldOriginal` after the init. `paletteBlocks` are the skinning matrices `g3d_ModelObj_CalculateSkeleton` (`0x112a5f0`) maps and flushes |
| `LocalTransform` (LocalMtx) | 0x60 | | `flags` bit 27 TRANS_ZERO (`kLocalTranslateZero`; by g3d, 26 is ROT_ZERO and 24/25 scale one / uniform, not followed); `MoveRootBone` clears bit 27 |
| `ShapeObj` | 0x70 | `g3d_ShapeObj_Initialize` `0x1131240` | `flags` bits 2 and 3 both set run `g3d_ShapeObj_CalculateBlendShape` (`0x1131cd0`). A `viewDependent` shape is skipped by `g3d_ModelObj_CalculateShape` (`0x112a7a0`) and calculated per view by the instance's virtual `+0x80`; `shapeBlockCount` is `viewCount` for such a shape, else 1. `shapeBlocks` index `view * bufferCount + buffer`: 0x100-byte blocks (world matrix, skin count, the user area). `boundings`: two `{centre, extent, radius}` copies per mesh, which the draws cull against. `buffer_40` (the builder's block 6) unknown |
| `MaterialObj` | 0x80 | `g3d_MaterialObj_Initialize` `0x1128aa0` | `srcParams` is a CPU copy of the FMAT's `+0x60` (FMAT `+0xa2` bytes), not uploaded again. `blockCopies` (2) is the dirty set's buffering count: the parameter block's copies, `blockSize` rounded up to 0x100 apart (`g3d_MaterialObj_CalculateMaterial` `0x1129240`). `paramDirty` is set up by `0x1129530`; `dirtyBufferMask` has a bit per buffer; `dirtyFlags` (`+0x2c`) bit 0 is set when any parameter is marked dirty; `+0x78` has no access |
| `MemoryPool` | 0x120 | `MemoryPool_Initialize` `0x11404f0` | nn::gfx TMemoryPool<NVN>: `nvnPool` points at `nvnStorage` (`+0x10`, the NVNmemoryPool, to `+0x110`), `cpu` (`+0x8`) the memory it is made on, `state` (`+0x110`) 1 once initialized (`MemoryPool_Finalize` `0x1140580` clears it). `gpu` (`+0x50`, inside the NVN pool) is measured (TESTING.md, Model object), not read statically. The graphics system's shared pool is at its `+0x7210` |

Play policies (AnimFrameCtrl `+0x10`): `g3d_PlayPolicy_Onetime` `0x11272d0`, `g3d_PlayPolicy_Loop` `0x11272f0`. An AnimFrameCtrl is `{f32 frame, start, end, step (the rate), policy function, user}`.

## Model data and instances

**ModelData** (`ModelData_Get` `0xff5080 (out, {resource, control block, model}*, int)`, vtable `0x1c45aa8` (base
`0x1c34d90`), 0x68 bytes, `ModelData_Dtor` `0xff73e0`): a model's data. `ModelData_Get` makes a new one on every call
and lists it in `ModelDataRegistry_ptr` (`0x1cc6f68` → a 0x88-byte object: a list at `+0x18`, count `+0x28`, mutex
`+0x68`); it looks nothing up. `refCount` is atomic; a model instance holds it as an intrusive pointer
(`ModelInstance::data`) and releases it through its virtual `+0x18`.

- `model` (with `modelCtrl`, a shared_ptr): the game's `Model`, made and initialized here.
- `vertexStates`: `vertexStateCount` (the shape count) nn::gfx VertexStates (0x30 each), one per shape, which the
  destructor finalizes (`VertexState_Finalize` `0x1146bb0`).
- `shapeShaders`: per shape the shader selector objects of its three shape binders (the third may be null), freed
  with `0x1027f10`; each has its current index at `+0x48` and 0x20-byte entries from `+0x8` (likely).
- `bufferCount`: the int argument (2 from `ModelInstance_Init`), the model's skeleton, shape and material
  buffering. `resModel` is the FMDL.

**Model** (0xd8 bytes, allocated by `ModelData_Get`): `obj` the nn::g3d ModelObj (0x90); `blockOffset` its block
buffer's offset in the graphics system's shared pool (`+0x7330`), `blockSize` (`g3d_ModelObj_CalculateBlockBufferSize`
`0x112a0f0`), `bufferSize`, `resource`, `resModel`, `initialized`, and `shapeInfos`: a `ShapeRenderInfo` (0x38) per
shape, from its material (FMAT array ResModel `+0x38`, by ResShape `+0x52`):

| Offset | |
|---|---|
| `+0x0` | `materialIndex` |
| `+0x4` | `renderLayer`: bits 0-1 pick the pass filter (0 to 3); above 1 drawn back to front |
| `+0x8` | `field_8` (0x20): copied from the material, not traced |
| `+0x28`..`+0x2c` | `castShadow` (pass 1), `optionEnable` (a shader option), `mainPasses` (passes 11 to 15, the third binder), `passFlag2b` (passes 16, 17), `water` (pass 10, the water binder) |
| `+0x2d` | `flag_2d`: not read |
| `+0x30` | `depthBias`: added to the rasterizer's depth bias |
| `+0x34` | `cullMode`: 3 keeps the pass's default |

The render passes (their ids are the draw context's `+0x188`) have no names in the binary; the flags are named by the
passes they select. Pass 1 is likely the shadow map (its depth bias is the larger of the draw's and the context's
`+0x278`).

**ModelInstance** (`ModelInstance_Make` `0x10361f0`, `ModelInstance_Ctor` `0x1036510`, `ModelInstance_Init` `0x1036900`,
`ModelInstance_Dtor` `0x1037350`, vtable `0x1c47640`; 0x3a0 bytes). Its base (vtable `0x1c346d0`, `+0x0`..`+0x60`; its
name virtual `+0x78` returns `RenderEntity`) is a drawable with pass and layer sets; its `+0x18` has no access found.
The init gets `data` through `ModelData_Get` and pushes one `shapeVisible` byte per shape.

The instanced subclass (`InstancedModelInstance_vtable` `0x1c34eb8`, 0x3c0 bytes, made by `InstancedModel_Setup`) adds `instanceCount` (`+0x394`, reset at each draw), `records` (`+0x398`) and `infoArray` (`+0x3b0`: an InstanceInfoArray, 0xa030 bytes, vtable `0x1c34ff8`, up to 0x200 records, count at `+0xc`) with its deleter. The plain instance's last field is `shadowPass` at `+0x390`; being 16-aligned (Vec4 members), it is 0x3a0 bytes with tail padding from `+0x391`, and the Itanium ABI puts the subclass's first field into that padding at `+0x394`. `game/model.hpp` models both as one flat struct of 0x3c0 bytes: the fields from `instanceCount` on exist only in an instanced instance.

| Virtual | |
|---|---|
| `+0x20` | Accept(visitor) |
| `+0x28` / `+0x30` | enable / disable a pass (`0xfd6260` / `0xfd62d0`); `+0x38` pass i, `+0x40` pass count |
| `+0x48` / `+0x50` | enable / disable a layer (`0xfd6300` / `0xfd6410`); `+0x70` clear the layers |
| `+0x80` | calculate the view-dependent shapes (`0x1037760`) |
| `+0x88` | `ModelInstance_Calc` (`0x1037780`): `calcOverride` when set, else `+0xc8`; then `afterCalc` |
| `+0x90` | draw: `ModelInstance_Draw` (`0x10377f0`); the instanced one's `InstancedModelInstance_Draw` (`0xfe4020`) |
| `+0x98` | instance count: 1; the instanced one's `InstancedModelInstance_Count` (`0xfe47d0`, `instanceCount`) |
| `+0xb8` | `ModelInstance_DrawShape`: draw shape i |
| `+0xc8` | `ModelInstance_CalcModel` (`0x1038030`): the world matrix from `position`, `rotation` and `scale`, then skeleton, materials, shapes, and `userParams` into every shape block's user area (`+0x40`..`+0xd0`, `userParam9` at `+0xd4`) |

`position`, `rotation` and `scale` are the model component's node world (`SkeletalModel_AddToModelList`,
`SkeletalModel_SyncWorld`), which set `transformDirty`. `lastPosition` follows `position` once it moved past a threshold
(then `0x1039210`). An animation layer that applies sets `skeletonDirty` (`0x1032970`) and emits `changed` (its virtual
`+0x30`). `renderMode` is 0 hidden, 1 normal (its passes enabled), 2 (`SkeletalModel_SetRenderMode`;
`ModelInstance_SetRenderMode` `0x1036df0`); `cullEnabled` and `culled` are the draw's culling. The bytes
`shapeCullMode`..`depthBias` are written through the skeletal model component's virtuals (below).

- `binders`: three vectors with an owned `ShapeBinder` per shape (`ModelInstance_MakeShapeBinders` `0x1038730`,
  virtual `+0xd0`). `[0]` the plain binder, or the water binder for a `water` shape; `[1]` the first kind; `[2]` the
  second kind with `mainPasses`, else null. `waterBinder` (`+0x108`) is a WaterShapeBinder only made and destroyed.
- `lightIndices` (up to 16) go into `lightIndexList` (a `UniformBlock` named `LightIndexList`, deleted by
  `lightIndexListDeleter`), padded with -1 and uploaded by `ModelInstance_CalcModel`.
- `shapeCullMode` (`+0x267`): 0 every visible shape drawn; 1 each shape culled against its bounds; 2 shapes with
  several meshes drawn per mesh (and `ModelInstance_UpdateBounds` `0x1039210` keeps each shape's bounds); the
  instanced draw tests the records' bounds only at 0.
- The pass bytes, all copied into each shape's draw item: `castShadow` (`+0x268`, 1 when made), `passFlagA`
  (`+0x26a`: passes 41, 42), `passFlagB` (`+0x26b`: 18, 44; with both clear 18, 19), `translucentPassFlag`
  (`+0x26c`: render layer 2's pass 22 instead of 20 and 21).
- The shader option bytes: `shapeOptionEnable` (`+0x269`, 1 when made), `shaderOption26f` (set by the model
  components' setups) and `envIndex` (`+0x27c`, from the renderer's environment object when the instance is added,
  `0x100e870`). `ModelInstance_UpdateShaderOptions` (`0x1039300 (instance, buffer)`, at the end of `CalcModel`) pushes
  them into every shape's shader selectors while the buffer's bit of `shaderDirtyMask` is set and clears it; a change
  sets the mask to 0x7fffffff.
- `depthBias` (a float) is added to every draw's rasterizer depth bias (`ModelInstance_SetRasterState` `0x103a160`).
- `boneSignals` (a libc++ `std::map` from bone index to an owned `Signal`) and `boneCallback` (`BoneCallback`, vtable
  `0x1c47728`: the map and the owner): `ModelInstance_ConnectBoneCallback` (`0x103c1a0 (instance, bone, Function*)`)
  makes the bone's signal, connects the function, sets `boneCallback` as the skeleton's `calcWorldCallback` (SkeletonObj
  `+0x58`) and the lowest key as its `callbackBone` (`+0x52`); `ModelInstance_BoneCallback_Exec` (`0x10370c0`) emits
  the bone's signal and moves on to the next key.

`ModelInstance_DrawShape` (`0x1037b90`, virtual `+0xb8`) fills a 0x90-byte draw item (`ShapeDrawItem_vtable`
`0x1c47870`: buffer index, shape, ShapeObj, ModelObj, the instance, the three binders, the pass flags, `+0x5c` the
depth bias, `+0x6c` the cull mode) and queues it by the pass mask of its render layer's filter. `ShapeDrawItem_Execute`
(`0x10399d0`) draws through binder `[1]` for passes 1, 10, 16, 18, 20 and 41, `[2]` for 11 and 13, `[0]` for the rest.
The item's `+0x10` is its sort key: bits 0-15 its view depth (render layers 0 and 1 front to back, 2 and 3 back to
front: `0xffff` minus it), bits 54-55 the render layer.

## Draw tree

The view's draw root is a tree of pass nodes (vtables from `0x1c45d58`; `+0x8` u8 pass id, `+0x9` enabled, `+0x40`
the children, `+0x58` the items). `DrawPass_Push` (`0xffc010`, virtual `+0x30`, `(node, item)`) adds the item to every
node whose pass bit is in the item's pass mask (`+0x8`), down the children. Drawing (virtual `+0x38`, `(node,
context)`): a group node (`DrawGroup_Draw`, `0xffc230`) sets the context's pass id (`+0x188`) from each enabled child
and draws it; a leaf pass binds its blend, depth-stencil and raster states (the leaf `0xffea20` the graphics holder's
`+0x5990`, `+0x5a10`, `+0x5a70`) and calls each item's `Execute`. The context caches what it bound
(`GfxContext_SetBlendState` `0xff93b0`, `GfxContext_SetDepthStencilState` `0xff93f0`, `GfxContext_SetRasterState`
`0xff9430`: `+0x90`, `+0x98`, `+0xa0`) and binds through its command buffer wrapper (`+0x8`; `CmdBuf_BindBlendState`
`0x113eb70`, `CmdBuf_BindDepthStencilState` `0x113ec20`, `CmdBuf_BindRasterState` `0x113ea70`); only the raster
state is rebuilt per item (`ModelInstance_SetRasterState`). A model drawn in the main view goes through passes 11, 12,
18 and 19 (render layers 0 and 1) and 13, 14, 20 and 21 (2 and 3), on two contexts side by side (11 to 14 and 18 to
21), each in that order.

nn::gfx `DepthStencilStateInfo` (`DepthStencilStateInfo_SetDefault` `0x113dfd0`, `DepthStencilState_Initialize`
`0x1146600 (state, device, info)`): `+0x0` the depth comparison function (nn::gfx `ComparisonFunction`: 1 less, 3 less
or equal, 6 greater or equal, 7 always; default 1), `+0x4` flags (bit 0 depth test, 1 depth write, 2 stencil test;
default none), `+0x8` and `+0x18` the front and back stencil. The game's depth is not reversed (near is less).

**ShapeBinder** (`ShapeBinder_Ctor` `0x102ebb0`, `ShapeBinder_vtable` `0x1c47250`, 0xa0 bytes; `ShapeBinder_Dtor`
`0x102ef70`): a shape's shader and uniform binder. Virtuals: `+0x20` `ShapeBinder_Draw` (`0x102f0e0`: fills the
buffer's block once per buffer index, `uploadedMask`), `+0x28` `ShapeBinder_BindShader` (`0x102f1f0`: the program and
`uniformBlocks` by name, `blockLocations` caching their locations), `+0x30` `ShapeBinder_BindTextures` (`0x102f5c0`:
the samplers through `samplerTable`, `_envSpec0` and `_envLocalSpec0` looked up once into `envSpecLocation` and
`envLocalSpecLocation`), `+0x38` `ShapeBinder_DrawMesh` (`0x102fb70`). `vertexStates` is the model data's;
`blockBuffers` hold one 0x800-byte buffer per buffer index. The water kind (`WaterShapeBinder_vtable` `0x1c47e68`)
also binds `_waterColor`, `_waterDepth` and `_reflectionColor`; the second kind (`ShapeBinder2_vtable` `0x1c479a8`)
binds `_depth`; the first is `ShapeBinder1_vtable` `0x1c47958`.

**UniformBlock** (`UniformBlock_vtable` `0x1c350a8`, `UniformBlock_Init` `0xfe5a70 (block, count, buffer count)`, 0x48
bytes): `count` × 16 u32 `words` (initialized to -1), one GPU buffer per buffer index in `gpuBuffers`, and its `name`
(a libc++ string), by which shape binders bind it. `UniformBlock_Upload` (`0xfe5960`, virtual `+0x28`) copies the
words into the buffer's GPU memory.

## Shape draw gate

Both draws, `ModelInstance_Draw` and `InstancedModelInstance_Draw`, draw shape i only while its `shapeVisible` byte is 1, its material's bit in `ModelObj::materialVisible` is set and its bone's bit in `ModelObj::boneVisible` is set (the shape's ResShape names both). `ModelInstance_Init` makes the bytes; `InstancedModel_Setup` sets them from `InstancedModelComponent::shapeMask`, and the shape setters (`SkeletalModel_SetShapeVisible`, virtual `+0xa0`; `SkeletalModel_SetShapeVisibleByName`, `+0x98`) change them.

## Skeletal model component

**Node** (`Node_Ctor` `0xfb55a0`, `Node_vtable` `0x1c31a68`; ACTORS.md, Nodes) is allocated as 0x150 bytes (`Actor_MakeNode` `0x20d10`) but its data ends at `+0x148`: it is 16-aligned (its Coords hold Vec4s), so its sizeof is 0x150 with tail padding from `+0x148`, and a derived class's first field goes there. The skeletal model component's `instance` and the character controller's second vtable (PLAYER.md) sit at `+0x148`. `game/actor.hpp` declares Node as 0x148 bytes so that the derived structs embed it.

**SkeletalModelComponent** (`SkeletalModelComponent_Make` `0x142660` allocates 0x300 bytes, `SkeletalModel_vtable` `0x1c4a060`, `SkeletalModel_Dtor` `0x104d780`):

- `SkeletalModel_Setup` (`0x105a550`, virtual `+0x20`) makes the instance from `modelResource` (`{resource, control
  block, model}`) through `ModelInstance_Make`, unless it exists already (`instanceShared`); builds the blend state
  (`BlendState_Build` `0x10156b0`), the depth-stencil state (`DepthStencilState_Initialize` `0x1146600`) and the
  rasterizer state (`RasterizerState_Initialize` `0x1146190`) and hands their pointers
  (`blendStatePtr`..`rasterizerStatePtr`) to the instance's `+0x68`..`+0x78`; sets the instance's `shaderOption26f`;
  connects to the node's `transformChanged` through its pointer at node `+0xf8` (`signalConnection`), whose callback
  (`0x105c060`) sets `worldDirty`. `SkeletalModel_SyncWorld` (`0x105aa40`) then copies the node's world into the
  instance.
- `SkeletalModel_Cleanup` (`0x105aac0`, virtual `+0x38`) disconnects and leaves a shared instance alone.
- `modelList` is a weak pointer to the renderer (`Framework::renderer`, FRAMEWORK.md): `SkeletalModel_AddToModelList` (`0x105aed0`, virtual `+0x40`) and `SkeletalModel_RemoveFromModelList` (`0x105b090`, `+0x48`) call the list's virtuals `+0x10` and `+0x18` on activation and deactivation.
- `order` is 0x401 (the animation components' 0x400); virtual `+0x0` returns it.
- The three states are wrappers `{nn::gfx state, the info it is built from, built}`: `blend` (`BlendStateObj`, 0x80:
  `BlendState` 0x28, `BlendStateInfo` 0x30 whose `targets` point into the setup's stack, one `BlendTargetStateInfo`
  0x20, `built` `+0x78`), `depthStencil` (`DepthStencilStateObj`, 0x60: state 0x1c, `DepthStencilStateInfo` 0x40 with
  front and back `StencilStateInfo`s, `built` `+0x5c`; the setup sets depth test and write) and `rasterizer`
  (`RasterizerStateObj`, 0x74: state 0x30 with the depth biases, `RasterizerStateInfo` 0x40, `built` `+0x70`; the
  setup sets the scissor flag). The infos' defaults: `BlendTargetStateInfo_SetDefault` `0x113df60`,
  `DepthStencilStateInfo_SetDefault` `0x113dfd0`, `RasterizerStateInfo_SetDefault` `0x113df10`.

| Virtual | |
|---|---|
| `+0x80` / `+0x88` | `SkeletalModel_SetRenderMode` (`0x105b7b0`, `renderMode`) / get it |
| `+0x90` | shape count |
| `+0x98` / `+0xa0` / `+0xa8` | set a shape visible by name / by index / is it visible |
| `+0xb0` | the shape's first mesh has more than one submesh |
| `+0xb8` / `+0xc0` | shape index by name / shape name |
| `+0xc8` / `+0xd0` | the instance's `depthBias` (get / set) |
| `+0xd8` | reset the materials |
| `+0xe0` | layer 1 on or off |
| `+0xe8`, `+0xf0`, `+0xf8`, `+0x100` | the instance's `shapeOptionEnable`, `passFlagA`, `passFlagB`, `shaderOption26f` (`+0xe8` and `+0x100` also set `shaderDirtyMask`) |
| `+0x108` / `+0x110` | Show / Hide |
| `+0x118` | `shadowPass` |
| `+0x120` | the instance's `translucentPassFlag` |
| `+0x128` / `+0x130`, `+0x138` / `+0x140` | `cullEnabled`, `shapeCullMode` (set / get) |
| `+0x148` / `+0x150` | `culled` / `forcePositionUpdate` |

## Instanced models

**InstancedModelComponent** (`InstancedModel_Ctor` `0x104d540`, `InstancedModel_vtable` `0x1c49340`, 0x310 bytes) is a skeletal model component whose vtable replaces the setup with `InstancedModel_Setup` (`0x104db10`): it looks the instance up in a cache of 0x40-byte entries (global `0x1cc6f78`) by model, `cacheKey` and `shapeMask`, else makes a new instanced instance, so components with the same key and mask share one instance. Its virtuals `+0x28` (`0x104ec60`) and `+0x40` (`InstancedModel_PushRecord` `0x104ef90`) push the node's world (node `+0xb0` position, `+0xc0` rotation, `+0xd0` scale) as an InstanceRecord, unless `added` is set or `visible` is clear; a per-frame task (`0x1050088`) clears `added`, so only a clear `visible` keeps the game from adding records. `visible`: virtuals `+0x80` set, `+0x88` get, `+0x108` Show, `+0x110` Hide (`0x104fa40`, `0x104fc70`, `0x104fc80`). Its `IsShapeVisible` (`+0xa8`, `0x104fb10`) tests `shapeMask`.

The skeletal model component's last field is `instanceShared` at `+0x2f8`: its sizeof is 0x300 with tail padding from `+0x2f9`, where the instanced component's fields begin (`added` `+0x2f9`, `visible` `+0x2fa`, `cacheKey` `+0x2fc`); `shapeMask` ends at `+0x304`, rounded up to 0x310. `game/model.hpp` therefore spells the base part out as bytes instead of embedding `SkeletalModelComponent`.

## Animation components

**BoundAnimation** (`Animation_Bind` `0x1033c00 (out, animation, model)`, 0x20 bytes, control block vtable `0x1c47400`): `obj` is an nn::g3d SkeletalAnimObj (0x90 bytes, `g3d_SkeletalAnimObj_vtable` `0x1c5e160`): `+0x8` its AnimFrameCtrl, `+0x68` the ResSkeletalAnim.

**AnimationLayer** (`AnimationLayer_vtable` `0x1c472f8`, 0x100 bytes; `AnimationLayer_Dtor` `0x1032810`). The layered
animation's layers are the masked kind (`MaskedAnimationLayer`, `MaskedAnimationLayer_vtable`
`0x1c47520`, 0x150 bytes, its fields in the plain layer's tail padding from `+0xfc`): `enabled` (1 when made;
`AnimationLayers_SetEnabled`), `additive` (`+0xfd`: the masked bones stay on the lower layers too, `0x1035390`; no
writer found), `boneMask` (`+0x100`, a u16 `{mask, effective}` per bone: `MaskedAnimationLayer_Init` `0x1035b40`,
`MaskedAnimationLayer_SetBoneMask` `0x1035e10`), `blendScratch` (`+0x118`, 0x30: a masked bone's transform that
`MaskedAnimationLayer_BlendInto` `0x1035f50` hands to the blender).

- `AnimationLayer_SetAnimation` (`0x1032e20`, virtual `+0xa0`, `(f32 blend, layer, animation sp*)`): the animation
  playing becomes `previous`, `blendFrames` the blend, `blendElapsed` 0; `rootMotion` takes the new one, its `delta`
  the identity and `hasLast` 0.
- `AnimationLayer_Calc` (`0x1032970`, `+0x10`): blends `previous` into `animation` while `blending`
  (`AnimationLayer_BlendPrevious` `0x10331e0`), applies the result to the skeleton through `blender` and sets the
  instance's `skeletonDirty`. `blender` is an nn::g3d SkeletalAnimBlender (0x20: `results` 0x50 per bone, `boneCount`
  the skeleton's, `flags` bits 0-1 the mode and bit 3 blending, `buffer`, a per-bone `callback`;
  `g3d_SkeletalAnimBlender_BlendBone` `0x1137a30`).
- `rootMotion` (`RootMotion`, 0x90, `RootMotion_vtable` `0x1c473e0`): `RootMotion_Calc` (`0x1033370`, from
  `AnimationLayer_Calc`) takes the root bone's motion since the last calc out of the animation into `delta` while
  `enabled` (`AnimationLayer_SetRootMotion` `0x1033040`, virtual `+0xb8`); it samples the root at `frame + rate ×
  sampleOffset` (through the play policy) and keeps `lastFrame`, `lastRoot` and `hasLast`.
- The advance (`0x1032a90`, `+0x18`) counts loops with `lastFrame` and `loopCount`.

| Virtual | |
|---|---|
| `+0x28` / `+0x30` | IsLoop / SetLoop |
| `+0x38` | loop count |
| `+0x40` | IsEnd(bool) |
| `+0x48` | `AnimationLayer_SetFrame` (`0x1032ca0`), through the play policy |
| `+0x50` / `+0x58` | the frame / the end frame |
| `+0x60` / `+0x68` | SetRate / GetRate |
| `+0x70` | name |
| `+0x78`, `+0x80`, `+0x88` | pause (`paused`) |
| `+0x90` | ended |
| `+0x98` | `model` |
| `+0xa8` | IsAnimation(ptr) |
| `+0xb0` | `blending` |
| `+0xb8` / `+0xc0` | `rootMotion.enabled`: `AnimationLayer_SetRootMotion` / get |
| `+0xc8` | sets `rootMotion.sampleOffset` |
| `+0xd0` | `&rootMotion.delta` (`0x10331d0`) |

**SkeletalAnimation** (SkeletalAnimationComponent; `NpcAnimator_Make` `0x9662e0`, `SkeletalAnimation_vtable`
`0x1c49d70`, 0x1b0 bytes, `SkeletalAnimation_Dtor` `0x1057fa0`): a component with one `layer` at `+0x60`, whose fields
it reads directly (`+0x150` paused, `+0x154` last frame, `+0x158` loop count). `SkeletalAnimation_BindModel`
(`0x1058130`, virtual `+0x20`) sets the layer's `instance` and `model` (seen from the component at `+0x88` and `+0x90`).
`modelComp` is a weak pointer, locked at each update (`0x1058eb0`). `onAnimationSet` (`std::function<void(animation sp,
int)>`) is the Tailor listener (TESTING.md, Effects and sounds), called by `SkeletalAnimation_SetAnimation`
(`0x10589e0`) and `LayeredAnimation_SetSlot`. `pad_58` and `+0x178` are alignment (the layer and the function are
16-aligned); `field_160` is zeroed by the
constructors and never read; `+0x1a8` is written nowhere.

| Virtual | |
|---|---|
| `+0x78` | register its tasks (`0x1058500`) |
| `+0x80` | `SkeletalAnimation_CountLoops` (`0x1058880`) |
| `+0x88` | `SkeletalAnimation_SetAnimation` |
| `+0x90` | updated |
| `+0x98` | IsAnimation |
| `+0xa0` / `+0xa8` / `+0xb0` | IsLoop / SetLoop / loop count |
| `+0xb8` | IsEnd |
| `+0xc0` / `+0xc8` / `+0xd0` | `SkeletalAnimation_SetFrame` (`0x1058ce0`) / the frame / the end frame |
| `+0xd8` / `+0xe0` | `SkeletalAnimation_SetRate` (`0x1058d60`) / GetRate |
| `+0xe8`, `+0xf0`, `+0xf8` | pause |
| `+0x100` | ended |
| `+0x108` / `+0x110` | `&layer` |
| `+0x118` | `SkeletalAnimation_Update` (`0x1058eb0`): the layer's Calc |

**AnimationLayers** (`AnimationLayers_Init` `0x1034bc0 (layers, count, model instance)`, `AnimationLayers_vtable` `0x1c47438`, 0x60 bytes, `AnimationLayers_Dtor` `0x1034260`): `count` is clamped to 1..8 and that many masked layers are made; `AnimationLayers_Calc` (`0x10342e0`) blends the enabled layers into the skeleton through `blender`. `paused` follows the layered animation's pause virtuals (`+0xe8`, `+0xf0`, `+0xf8`, `0x1059b70`).

**LayeredAnimation** (`LayeredAnimation_Make` `0x67ba70` allocates 0x220 bytes; `LayeredAnimation_Ctor` `0x1059200 (animation, owner, model component sp*, layer count)`, `LayeredAnimation_vtable` `0x1c49ee8`, `LayeredAnimation_Dtor` `0x1059460`): a SkeletalAnimation whose own layer at `+0x60` is unused, and the AnimationLayers at `+0x1b0`. `requestedLayers` is the constructor's count until `LayeredAnimation_BindModel` (`0x10594e0`, `+0x20`) binds the model. `LayeredAnimation_SetSlot` (`0x1059dd0`) sets an animation on a layer; `LayeredAnimation_Update` (`0x1059cf0`, `+0x118`). Link's has 2 layers, layer 1 masked from `spine_a` (PLAYER.md).

**MaterialAnim** (`MaterialAnim_Bind` `0xfe70a0 (out sp, animation entry, model component)`, `MaterialAnim_vtable` `0x1c35150`, base `0x1c351e0`; 0x28 bytes): `obj` is an nn::g3d MaterialAnimObj (0xa0 bytes, `g3d_MaterialAnimObj_vtable` `0x1c5dc08`; its AnimFrameCtrl at `+0x8`), null when binding failed.

**MatAnimSlot** (`MatAnimSlot_vtable` `0x1c35300`, 0x30 bytes): `anim` is set by the slot setter (`0x1055760`), which also zeroes `lastFrame`. `MatAnimSlot_Update` (`0xfe6d90`, virtual `+0x18`) records the frame in `lastFrame`, so it is the frame at the slot's last update: a frame behind, and not updated while paused; the live frame is virtual `+0x50`. `applied` is set after an apply.

| Virtual | |
|---|---|
| `+0x10` / `+0x18` | update |
| `+0x28` / `+0x30` | IsLoop / SetLoop |
| `+0x48` / `+0x50` / `+0x58` | SetFrame / the frame / the end frame |
| `+0x60` / `+0x68` | SetRate / GetRate |
| `+0x70` | name |
| `+0x78`, `+0x80`, `+0x88` | pause (`paused`) |
| `+0x98` | `model` |
| `+0xa0` | IsAnimation |

**MaterialAnimComponent** (MultiMaterialAnimation; `NpcComponent_MakeMaterialAnimator` `0x9535f0`, `MultiMaterialAnimation_vtable` `0x1c499f0`, base `MaterialAnimation_vtable` `0x1c49850` with one slot; 0xc0 bytes, `MultiMaterialAnimation_Dtor` `0x1054500`): `baseSlot` is the base class's slot, unused here. `MultiMaterialAnimation_Setup` (`0x1054710`, virtual `+0x20`) makes `slotCount` slots (clamped to 1..8) with the model object of `modelComp`. `MultiMaterialAnimation_Update` (`0x1055190`, `+0x80`) calls each slot's `+0x10`, or `+0x18` while `paused`; `MultiMaterialAnimation_Apply` (`0x10552d0`, `+0x88`) applies each slot's animation to the model (the animation's virtual `+0x28`) and sets the slot's `applied`. Virtual `+0x90` sets slot 0; the virtuals from `+0xb8` forward to slot 0. An NPC's slot count is its facial row's (ACTORS.md, NPCs) plus one; Link's is 7 (TESTING.md, Tunic colour).

## Open questions

- The render passes' names; whether pass 1 is the shadow map.
- The shader selectors' classes: the option setters of the only vtable found (`0x1c458a8`) are empty, so what
  `shapeOptionEnable`, `shaderOption26f` and `envIndex` switch is open.
- No access found: `ModelInstance` `+0x18`, `ModelObj` `field_20` and `field_28`, `MaterialObj` `+0x78`.
- `ShapeRenderInfo` `field_8` and the material record it is copied from; the contents of `ShapeBinder`'s `blocks`
  and `samplerTable`.
- `MemoryPool` `+0x112`..`+0x120`; where in the NVN pool `gpu` lives (`+0x50` is measured).
- The masked layer's `additive`: no writer found.
