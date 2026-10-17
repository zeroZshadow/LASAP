"""Multiplayer ghosts (DESIGN.md, Ghosts): the model and the rows the module draws other players with.

PlayerGhost.bfres is Link's model (Playerlink.bfres) made for a ghost: the albedo textures of the materials a ghost
shows (Link and the items on his model: the module shows those its player shows) as BC3 with a constant alpha (their
colour blocks kept) in render pass 1, so the module's alpha-to-coverage dithers them in screen space; the red and blue
tunics at half size and the items' other maps (normal, smoothness, metalness; the mirror shield keeps its, without
them it draws black) and the textures of the items that are other models shrunk to 4x4 to make room. Building it takes native packages, so it is an asset (ap_assets.py).

The row APGhost is NpcGhost's row drawing that model with Link's animations, with no collision, action, event
triggers or conditions. The module spawns ghosts as NpcGhost actors that read the APGhost row. It is copied from
the game's NpcGhost row before the Ammo Ghost option (ap_ghost.py) rewrites that one. Its facial row, APGhost in
Facial.gsheet, names Link's face animations (eye, mouth, ear) so the NPC gets a material animation slot for each,
held at frame 0 until the module sets the sender's frames.
"""
import copy
import io
import struct

SOURCE = "Playerlink.bfres"
FILE_NAME = "PlayerGhost.bfres"
MODEL_NAME = "link"
ROW = "APGhost"
FROM_ROW = "NpcGhost"
FACIAL_FROM_ROW = "Ghost"  # NpcGhost's facial row, the APGhost one's base
# The face's material animations on Link's model (eye, mouth, then the others): the facial row gives the NPC a slot
# for each
FACE_ANIMATIONS = ("eye", "mouth", "ear")
ALPHA = 128
# Albedo textures of the shown materials, made BC3 with ALPHA (a BC3 one keeps its alpha, scaled by ALPHA)
ALPHA_TEXTURES = ("MI_body_alb.0", "MI_body_alb.1", "MI_body_alb.2", "MI_hair_alb", "MI_shieldA_alb", "MI_sowrdA_alb",
                  "MI_magicRod_alb", "MI_ocarina_alb", "MI_shovel_alb", "MI_flippers_alb", "MI_hookShot_alb",
                  "MI_shieldB_alb", "MI_shieldBMirror_alb", "MI_sowrdB_alb")
ALPHA_PREFIXES = ("MI_ear_alb", "MI_eye_alb", "MI_mouth_alb")
# Without their largest mip level: the red and blue tunics (material animation "clothes")
HALF_TEXTURES = ("MI_body_alb.1", "MI_body_alb.2")
# Material names in texture names: their other textures (normal, mask maps) stay as they are
SHOWN_TEXTURE_PARTS = ("MI_body", "MI_ear", "MI_eye", "MI_mouth", "MI_hair", "MI_shieldA", "MI_sowrdA", "MI_boots",
                       "MI_shieldB")
# The materials drawn in render pass 1 (the module's kShownMaterials)
SHOWN_MATERIALS = ("MI_body", "MI_boots", "MI_ear", "MI_eye", "MI_mouth", "MI_hair", "linkShieldA_MI_shieldA",
                   "linkSwordA_MI_sowrdA", "linkSwordA_MI_sowrdAball", "MI_flippers", "MagicRod_MI_magicRod",
                   "Ocarina_MI_ocarina", "Shovel_MI_shovel", "linkHookShot_MI_hookShot", "linkShieldB_MI_shieldB",
                   "linkShieldB_MI_shieldBMirror", "linkSwordB_MI_sowrdB", "linkSwordB_MI_sowrdBball")
FORMAT_FOURCC = {0x1a06: b"DXT1", 0x1a01: b"DXT1", 0x1c06: b"DXT5", 0x1c01: b"DXT5", 0x1d01: b"BC4U",
                 0x1e01: b"BC5U", 0x1e02: b"BC5S"}
# The row's animations: the NPC component's own (idle, talk, walk, run) by Link's names
ANIMATIONS = {"idle": "wait", "talk": "talk", "walk": "walk", "run": "run"}


def dds(width: int, height: int, fourcc: bytes, levels: list) -> io.BytesIO:
    header = bytearray(128)
    header[0:4] = b"DDS "
    flags = 0x81007 | (0x20000 if len(levels) > 1 else 0)
    struct.pack_into("<7I", header, 4, 124, flags, height, width, len(levels[0]), 0, len(levels))
    struct.pack_into("<2I4s", header, 76, 32, 4, fourcc)
    struct.pack_into("<I", header, 108, 0x1000 | (0x400008 if len(levels) > 1 else 0))
    return io.BytesIO(bytes(header) + b"".join(levels))


def bc1_to_bc3(blocks: bytes, alpha: int) -> bytes:
    """BC1 blocks as BC3 blocks with a constant alpha. BC3 reads every colour block in 4-colour mode, so 3-colour
    blocks are rewritten: the half mix becomes the 2/3 mix and transparent black the darker end."""
    out = bytearray()
    alpha_block = bytes((alpha, alpha, 0, 0, 0, 0, 0, 0))
    for k in range(0, len(blocks), 8):
        c0, c1, bits = struct.unpack_from("<HHI", blocks, k)
        if c0 <= c1:
            dark = 0 if c0 < c1 else 1
            new_bits = 0
            for p in range(16):
                new_bits |= {0: 0, 1: 1, 2: 2, 3: dark}[(bits >> (2 * p)) & 3] << (2 * p)
            if c0 == c1:
                new_bits = 0
            if c0 < c1:  # 4-colour mode needs c0 > c1: swap the endpoints and indices 0<->1, 2<->3
                c0, c1 = c1, c0
                new_bits ^= 0x55555555
            elif c0 > 0:
                c1 = c0 - 1
            bits = new_bits
        out += alpha_block + struct.pack("<HHI", c0, c1, bits)
    return bytes(out)


def bc3_scale_alpha(blocks: bytes, alpha: int) -> bytes:
    """BC3 blocks with their alpha endpoints scaled by alpha / 255 (the 6-step mode kept: its endpoints stay apart)."""
    out = bytearray(blocks)
    for k in range(0, len(out), 16):
        a0, a1 = out[k], out[k + 1]
        b0, b1 = a0 * alpha // 255, a1 * alpha // 255
        if a0 > a1 and b0 == b1:
            b0 = b1 + 1
        out[k], out[k + 1] = b0, b1
    return bytes(out)


def set_render_pass(data: bytearray, materials: tuple, value: int) -> int:
    """Sets the FMAT shader option "pass" of the named materials; returns how many it set."""
    def name_at(p: int) -> str:
        length = struct.unpack_from("<H", data, p)[0]
        return bytes(data[p + 2:p + 2 + length]).decode("latin1")

    def is_name(p: int) -> bool:
        if not 0 < p < len(data) - 2:
            return False
        length = struct.unpack_from("<H", data, p)[0]
        return 0 < length < 64 and all(32 <= c < 127 for c in data[p + 2:p + 2 + length])

    count = 0
    i = data.find(b"FMAT")
    while i >= 0:
        name = struct.unpack_from("<Q", data, i + 8)[0]
        if is_name(name) and name_at(name) in materials:
            for k in range(data[i + 0x9a]):
                entry = struct.unpack_from("<Q", data, i + 0x10)[0] + k * 0x18
                option_name, value_at = struct.unpack_from("<QQ", data, entry)
                if name_at(option_name) == "pass":
                    struct.pack_into("<i", data, value_at, value)
                    count += 1
        i = data.find(b"FMAT", i + 4)
    return count


def build(source: str) -> bytes:
    """PlayerGhost.bfres from the path of Playerlink.bfres (needs the native packages)."""
    from RandomizerCore.Tools.bntx_editor import bfres, bntx_editor

    model = bfres.File()
    model.readFromFile(source)
    start, size = model.bfresTexture.pos, model.bfresTexture.size
    editor = bntx_editor.BNTXEditor()
    editor.openFile(bytes(model.rawData[start:start + size]))
    textures = editor.bntx
    for index, texture in enumerate(list(textures.textures)):
        name = texture.name
        alpha = name in ALPHA_TEXTURES or name.startswith(ALPHA_PREFIXES)
        shown = any(name.startswith(m + "_") for m in SHOWN_TEXTURE_PARTS) and not name.startswith("MI_body_alb")
        new = None
        if alpha and texture.format_ in (0x1a06, 0x1a01, 0x1c06, 0x1c01):
            levels, _, _ = textures.rawData(texture)
            width, height = texture.width, texture.height
            if name in HALF_TEXTURES:
                levels, width, height = levels[1:], width // 2, height // 2
            convert = bc1_to_bc3 if texture.format_ in (0x1a06, 0x1a01) else bc3_scale_alpha
            converted = dds(width, height, b"DXT5", [convert(bytes(l), ALPHA) for l in levels])
            new = textures.replace(texture, texture.tileMode, (texture.format_ & 0xff) == 6, False, False, True, converted)
            if not new:
                raise ValueError(f"could not convert {name}")
        elif not alpha and not shown and texture.format_ in FORMAT_FOURCC:
            fourcc = FORMAT_FOURCC[texture.format_]
            block = 16 if fourcc in (b"DXT5", b"BC5U", b"BC5S") else 8
            new = textures.replace(texture, texture.tileMode, (texture.format_ & 0xff) == 6, False, False, False,
                                   dds(4, 4, fourcc, [bytes(block)]))
        if new:
            textures.textures[index] = new
    packed = editor.save()
    if len(packed) > size:
        raise ValueError("the textures do not fit the model file")
    raw = bytearray(model.rawData)
    raw[start:start + len(packed)] = packed
    if set_render_pass(raw, SHOWN_MATERIALS, 1) == 0:
        raise ValueError("no render pass option found")
    return bytes(raw)


def ghost_row(row: dict) -> dict:
    """The APGhost row made from NpcGhost's (parsed) row."""
    row["symbol"] = ROW
    graphics = row["graphics"]
    graphics["path"] = FILE_NAME
    graphics["model"] = MODEL_NAME
    graphics["animations"] = dict(ANIMATIONS)
    graphics["animSettings"] = [{"name": name, "blendTime": 4, "lipsync": 0, "cull": 2}
                                for name in ANIMATIONS.values()]
    graphics["facial"] = ROW
    row["eventTriggers"] = []
    row["doAction"]["type"] = 0  # no talk or other action, so no DoAction component
    row["popConditions"]["conditions"] = []
    row["layoutConditions"] = []
    row["collision"]["shape"] = 0  # no Collision component: nothing blocks Link, nothing moves the ghost
    row["damageCollision"]["enable"] = False
    return row


def add_row(sheet: dict) -> None:
    """Adds the APGhost row to a read Npc.gsheet (replacing one there), from its NpcGhost row."""
    from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct

    rows = [parseStruct(value) for value in sheet["values"]]
    source = next((row for row in rows if row["symbol"] == FROM_ROW), None)
    if source is None:
        raise ValueError(f"no {FROM_ROW} row")
    sheet["values"] = [value for value, row in zip(sheet["values"], rows) if row["symbol"] != ROW]
    sheet["values"].append(dictToStruct(ghost_row(source)))


def add_facial_row(sheet: dict) -> None:
    """Adds the APGhost row to a read Facial.gsheet (replacing one there): Link's face animations, held at frame 0
    with no blinking or lip sync (the module sets their frames from the sender's)."""
    from RandomizerCore.Tools import gsheet
    from RandomizerCore.Tools.oead_tools import parseStruct

    rows = [parseStruct(value) for value in sheet["values"]]
    source = next((value for value, row in zip(sheet["values"], rows) if row["mKey"] == FACIAL_FROM_ROW), None)
    if source is None:
        raise ValueError(f"no {FACIAL_FROM_ROW} facial row")
    row = copy.deepcopy(source)
    row["mKey"] = ROW
    eye, mouth, *others = FACE_ANIMATIONS
    names = row["mMaterialAnimNames"]
    names["mEye"], names["mMouth"], names["mOthers"] = eye, mouth, gsheet.StringArray(others)
    normal = row["mExpressions"][0]
    normal.update(mLabel="normal", mEyeFrame=0, mMouthFrame=0, mEyeLength=1, mMouthLength=1, mEyeBlinkFrame=0,
                  mEyeBlinkSettings=None, mLipSyncCloseFrame=0, mLipSyncOpenFrame=0, mLipSyncSettings=None,
                  mOtherFrames=gsheet.IntArray([0] * len(others)), mOtherLengths=gsheet.IntArray([1] * len(others)))
    row["mExpressions"] = gsheet.StructArray([normal])
    sheet["values"] = [value for value, r in zip(sheet["values"], rows) if r["mKey"] != ROW]
    sheet["values"].append(row)


def add(files) -> None:
    """At patch time, before ap_ghost.add: the APGhost rows in the mod's Npc.gsheet and Facial.gsheet."""
    sheet = files.readFile("Npc.gsheet")
    add_row(sheet)
    files.writeFile("Npc.gsheet", sheet)
    facial = files.readFile("Facial.gsheet")
    add_facial_row(facial)
    files.writeFile("Facial.gsheet", facial)
