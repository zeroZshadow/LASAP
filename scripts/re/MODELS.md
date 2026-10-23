# Models and animation (game 1.0.0)

The game's model objects (nn::g3d), the model instances the renderer draws, the model components that hold them and the animation components that pose them, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. Field names are those of `game/model.hpp` and `game/animation.hpp`, which list every field; this file gives the evidence and what their comments leave out. "Guess" marks what the code suggests but was not followed to the end. TESTING.md, Reverse engineering, has Link's model, his shapes, the model material's parameter block and the instanced draw; this file does not repeat them.

## nn::g3d objects

The layouts follow NX nn::g3d: ResShape's material index at `+0x52` and bone index at `+0x54`, LocalMtx's flag bit 27 (TRANS_ZERO), AnimFrameCtrl's play policy function pointers. Matrices are nn::util SIMD types (Matrix4x3f, 4 × float4 = 0x40 bytes).

| Object | Size | Initialize | |
|---|---|---|---|
| `ModelObj` | at least 0x90 | `g3d_ModelObj_Initialize` `0x1129a30 (model, arg, buffer, size)` | built in the g3d builder's buffer, so its size is not read. Makes `shapes` (0x70 apart) and `materials` (0x80 apart), and the `skeleton` in the same buffer (0x80 bytes, zeroed) unless the argument passes one. `boneVisible` from ResBone `+0x34` bit 0; `hasBillboardShape` set when a shape's bone has billboard bits (ResBone `+0x34 & 0x70000`), read by the instance's view-dependent calculation. `g3d_ModelObj_SetupBlockBuffer` (`0x112a210`) writes `pool` and `poolOffset` and sets `flags` bit 0. The two visibility callbacks are called `(model, index)` when a bit changes. `forceRigidCalc` gives skinned shapes `CalculateShape` too (`0x112a7a0`, `0x1037760`) |
| `Skeleton` (SkeletonObj) | 0x80 | `g3d_SkeletonObj_Initialize` `0x1139ed0` | `res` the FSKL: `+0x10` ResBone array (0x60 each), `+0x18` matrix-to-bone s16 table, `+0x20` inverse bind matrices (3 × 4), `+0x38` bone count, `+0x3a` smooth count, `+0x3c` rigid count. `flags` from ResSkeleton `+0x4`; byte `+0x9 & 3` picks the world calculation (`g3d_ModelObj_CalculateWorld` `0x112a4b0`). `locals` and `world` equal `localsOriginal` and `worldOriginal` after the init. `paletteBlocks` are the skinning matrices `g3d_ModelObj_CalculateSkeleton` (`0x112a5f0`) maps and flushes |
| `LocalTransform` (LocalMtx) | 0x60 | | `flags` bit 27 TRANS_ZERO (`kLocalTranslateZero`; by g3d, 26 is ROT_ZERO and 24/25 scale one / uniform, not followed); `MoveRootBone` clears bit 27 |
| `ShapeObj` | 0x70 | `g3d_ShapeObj_Initialize` `0x1131240` | `flags` bits 2 and 3 both set run `g3d_ShapeObj_CalculateBlendShape` (`0x1131cd0`). A `viewDependent` shape is skipped by `g3d_ModelObj_CalculateShape` (`0x112a7a0`) and calculated per view by the instance's virtual `+0x80`; `shapeBlockCount` is `viewCount` for such a shape, else 1. `shapeBlocks` index `view * bufferCount + buffer`: 0x100-byte blocks (world matrix, skin count, the user area). `boundings`: two `{centre, extent, radius}` copies per mesh, which the draws cull against. `buffer_40` (the builder's block 6) unknown |
| `MaterialObj` | 0x80 | `g3d_MaterialObj_Initialize` `0x1128aa0` | `srcParams` is a CPU copy of the FMAT's `+0x60` (FMAT `+0xa2` bytes), not uploaded again. `blockCopies` (2) is the dirty set's buffering count: the parameter block's copies, `blockSize` rounded up to 0x100 apart (`g3d_MaterialObj_CalculateMaterial` `0x1129240`). `paramDirty` is set up by `0x1129530`; `dirtyBufferMask` has a bit per buffer |
| `MemoryPool` | not mapped | | nn::gfx MemoryPool: `cpu` `+0x8` and `gpu` `+0x50` are measured (TESTING.md, Model object); no static read of `+0x50` found |

Play policies (AnimFrameCtrl `+0x10`): `g3d_PlayPolicy_Onetime` `0x11272d0`, `g3d_PlayPolicy_Loop` `0x11272f0`. An AnimFrameCtrl is `{f32 frame, start, end, step (the rate), policy function, user}`.

## Model data and instances

**ModelData** (`ModelData_Get` `0xff5080 (out, {resource, control block, model}*, int)`, vtable `0x1c45aa8` (base `0x1c34d90`), 0x68 bytes, `ModelData_Dtor` `0xff73e0`): a model's shared data, cached per resource. `refCount` is atomic; a model instance holds it as an intrusive pointer (`ModelInstance::data`) and releases it through its virtual `+0x18`. `gfxObjects` are `gfxObjectCount` gfx objects the destructor finalizes on the device; `shaderSets` holds three owned objects per element (freed with `0x102ff10`); `mode` is the int argument (2 from `ModelInstance_Init`). `+0x18` and `+0x60` unknown.

**ModelInstance** (`ModelInstance_Make` `0x10361f0`, `ModelInstance_Ctor` `0x1036510`, `ModelInstance_Init` `0x1036900`, `ModelInstance_Dtor` `0x1037350`, vtable `0x1c47640`; 0x3a0 bytes). Its base (vtable `0x1c346d0`, `+0x0`..`+0x60`) is a drawable with pass and layer sets. The init gets `data` through `ModelData_Get` and pushes one `shapeVisible` byte per shape.

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
| `+0xb8` | draw shape i |
| `+0xc8` | `ModelInstance_CalcModel` (`0x1038030`): the world matrix from `position`, `rotation` and `scale`, then skeleton, materials, shapes, and `userParams` into every shape block's user area (`+0x40`..`+0xd0`, `userParam9` at `+0xd4`) |

`position`, `rotation` and `scale` are the model component's node world (`SkeletalModel_AddToModelList`, `SkeletalModel_SyncWorld`), which set `transformDirty`. `lastPosition` follows `position` once it moved past a threshold (then `0x1039210`). An animation layer that applies sets `skeletonDirty` (`0x1032970`) and emits `changed` (its virtual `+0x30`). `renderMode` is 0 hidden, 1 normal (its passes enabled), 2 (`SkeletalModel_SetRenderMode`); `cullEnabled` and `culled` are the draw's culling; with `field_267` set the instanced draw skips each record's bounds. The bytes `field_268`..`field_274` are written through the skeletal model component's virtuals (below); their readers are not traced.

## Shape draw gate

Both draws, `ModelInstance_Draw` and `InstancedModelInstance_Draw`, draw shape i only while its `shapeVisible` byte is 1, its material's bit in `ModelObj::materialVisible` is set and its bone's bit in `ModelObj::boneVisible` is set (the shape's ResShape names both). `ModelInstance_Init` makes the bytes; `InstancedModel_Setup` sets them from `InstancedModelComponent::shapeMask`, and the shape setters (`SkeletalModel_SetShapeVisible`, virtual `+0xa0`; `SkeletalModel_SetShapeVisibleByName`, `+0x98`) change them.

## Skeletal model component

**Node** (`Node_Ctor` `0xfb55a0`, `Node_vtable` `0x1c31a68`; ACTORS.md, Nodes) is allocated as 0x150 bytes (`Actor_MakeNode` `0x20d10`) but its data ends at `+0x148`: it is 16-aligned (its Coords hold Vec4s), so its sizeof is 0x150 with tail padding from `+0x148`, and a derived class's first field goes there. The skeletal model component's `instance` and the character controller's second vtable (PLAYER.md) sit at `+0x148`. `game/actor.hpp` declares Node as 0x148 bytes so that the derived structs embed it.

**SkeletalModelComponent** (`SkeletalModelComponent_Make` `0x142660` allocates 0x300 bytes, `SkeletalModel_vtable` `0x1c4a060`, `SkeletalModel_Dtor` `0x104d780`):

- `SkeletalModel_Setup` (`0x105a550`, virtual `+0x20`) makes the instance from `modelResource` (`{resource, control block, model}`) through `ModelInstance_Make`, unless it exists already (`instanceShared`); builds the blend state (`BlendState_Build` `0x10156b0`), the depth-stencil state (`DepthStencilState_Initialize` `0x1146600`) and the rasterizer state (`RasterizerState_Initialize` `0x1146190`) and hands their pointers (`blendStatePtr`..`rasterizerStatePtr`) to the instance's `+0x68`..`+0x78`; sets the instance's `field_26f`; connects to the node's `transformChanged` through its pointer at node `+0xf8` (`signalConnection`), whose callback (`0x105c060`) sets `worldDirty`. `SkeletalModel_SyncWorld` (`0x105aa40`) then copies the node's world into the instance.
- `SkeletalModel_Cleanup` (`0x105aac0`, virtual `+0x38`) disconnects and leaves a shared instance alone.
- `modelList` is a weak pointer to the renderer (`Framework::renderer`, FRAMEWORK.md): `SkeletalModel_AddToModelList` (`0x105aed0`, virtual `+0x40`) and `SkeletalModel_RemoveFromModelList` (`0x105b090`, `+0x48`) call the list's virtuals `+0x10` and `+0x18` on activation and deactivation.
- `order` is 0x401 (the animation components' 0x400); virtual `+0x0` returns it.

| Virtual | |
|---|---|
| `+0x80` / `+0x88` | `SkeletalModel_SetRenderMode` (`0x105b7b0`, `renderMode`) / get it |
| `+0x90` | shape count |
| `+0x98` / `+0xa0` / `+0xa8` | set a shape visible by name / by index / is it visible |
| `+0xb0` | the shape's first mesh has more than one submesh |
| `+0xb8` / `+0xc0` | shape index by name / shape name |
| `+0xc8` / `+0xd0` | the instance's `field_274` |
| `+0xd8` | reset the materials |
| `+0xe0` | layer 1 on or off |
| `+0xe8`, `+0xf0`, `+0xf8`, `+0x100` | the instance's bytes `+0x269`, `+0x26a`, `+0x26b`, `+0x26f` (`+0xe8` also resets `field_270`) |
| `+0x108` / `+0x110` | Show / Hide |
| `+0x118` | `shadowPass` |
| `+0x120` | the instance's byte `+0x26c` |
| `+0x128` / `+0x130`, `+0x138` / `+0x140` | `cullEnabled`, `field_267` |
| `+0x148` / `+0x150` | `culled` / `forcePositionUpdate` |

## Instanced models

**InstancedModelComponent** (`InstancedModel_Ctor` `0x104d540`, `InstancedModel_vtable` `0x1c49340`, 0x310 bytes) is a skeletal model component whose vtable replaces the setup with `InstancedModel_Setup` (`0x104db10`): it looks the instance up in a cache of 0x40-byte entries (global `0x1cc6f78`) by model, `cacheKey` and `shapeMask`, else makes a new instanced instance, so components with the same key and mask share one instance. Its virtuals `+0x28` (`0x104ec60`) and `+0x40` (`InstancedModel_PushRecord` `0x104ef90`) push the node's world (node `+0xb0` position, `+0xc0` rotation, `+0xd0` scale) as an InstanceRecord, unless `added` is set or `visible` is clear; a per-frame task (`0x1050088`) clears `added`, so only a clear `visible` keeps the game from adding records. `visible`: virtuals `+0x80` set, `+0x88` get, `+0x108` Show, `+0x110` Hide (`0x104fa40`, `0x104fc70`, `0x104fc80`). Its `IsShapeVisible` (`+0xa8`, `0x104fb10`) tests `shapeMask`.

The skeletal model component's last field is `instanceShared` at `+0x2f8`: its sizeof is 0x300 with tail padding from `+0x2f9`, where the instanced component's fields begin (`added` `+0x2f9`, `visible` `+0x2fa`, `cacheKey` `+0x2fc`); `shapeMask` ends at `+0x304`, rounded up to 0x310. `game/model.hpp` therefore spells the base part out as bytes instead of embedding `SkeletalModelComponent`.

## Animation components

**BoundAnimation** (`Animation_Bind` `0x1033c00 (out, animation, model)`, 0x20 bytes, control block vtable `0x1c47400`): `obj` is an nn::g3d SkeletalAnimObj (0x90 bytes, `g3d_SkeletalAnimObj_vtable` `0x1c5e160`): `+0x8` its AnimFrameCtrl, `+0x68` the ResSkeletalAnim.

**AnimationLayer** (`AnimationLayer_vtable` `0x1c472f8`, 0x100 bytes; `AnimationLayer_Dtor` `0x1032810`). The layered animation's layers are the masked kind (`MaskedAnimationLayer_vtable` `0x1c47520`, 0x150 bytes): `+0xfc` enabled (`AnimationLayers_SetEnabled`), `+0xfd` skips the mask merge (`0x1035390`), `+0x100` a vector of `{mask, effective}` per bone (`MaskedAnimationLayer_Init` `0x1035b40`, `MaskedAnimationLayer_SetBoneMask` `0x1035e10`); `+0x118`..`+0x150` unknown.

- `AnimationLayer_SetAnimation` (`0x1032e20`, virtual `+0xa0`, `(f32 blend, layer, animation sp*)`): the animation playing becomes `previous`, `blendFrames` the blend, `blendElapsed` 0, `offset` the identity, `field_e4` 0; `heldAnimation` holds the new one (an object of vtable `0x1c473e0` and a shared pointer).
- `AnimationLayer_Calc` (`0x1032970`, `+0x10`): blends `previous` into `animation` while `blending`, applies the result to the skeleton through `blender` (nn::g3d SkeletalAnimBlender: `+0xa` u16 bone count, `+0xc` flags, `+0x10` its buffer) and sets the instance's `skeletonDirty`.
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
| `+0xb8` / `+0xc0` | `field_ec` |
| `+0xc8` | `weight` (guess) |
| `+0xd0` | `&offset` (`0x10331d0`) |

**SkeletalAnimation** (SkeletalAnimationComponent; `NpcAnimator_Make` `0x9662e0`, `SkeletalAnimation_vtable` `0x1c49d70`, 0x1b0 bytes, `SkeletalAnimation_Dtor` `0x1057fa0`): a component with one `layer` at `+0x60`, whose fields it reads directly (`+0x150` paused, `+0x154` last frame, `+0x158` loop count). `SkeletalAnimation_BindModel` (`0x1058130`, virtual `+0x20`) sets the layer's `instance` and `model` (seen from the component at `+0x88` and `+0x90`). `modelComp` is a weak pointer, locked at each update (`0x1058eb0`). `onAnimationSet` (`std::function<void(animation sp, int)>`) is the Tailor listener (TESTING.md, Effects and sounds), called by `SkeletalAnimation_SetAnimation` (`0x10589e0`) and `LayeredAnimation_SetSlot`. `pad_58`, `field_160`, `+0x178` and `+0x1a8` are written nowhere or only by the constructor.

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

- `ModelObj` `+0x1c`..`+0x30` and `field_8c` (InitializeArgument `+0x20`); its exact size.
- `MemoryPool` beyond the measured `+0x8` and `+0x50`.
- `ModelInstance`: `owned`, `object_108` (vtable `0x1c47e68`, constructor `0x102ebb0`), `vector_1a8`, `object_1c0` (a 0x48-byte object, vtable `0x1c350a8`), `map_2d8`, `object_2f0`, and the readers of `field_268`..`field_274`.
- `AnimationLayer` `+0x78`, the use of `offset`, `+0xb0`..`+0xe0`, `field_e4`, `field_ec`; the masked layer's `+0x118`..`+0x150`.
- `ModelData` `+0x18`, the element type of `shaderSets`, `+0x60`.
