"""Multiplayer ghosts (DESIGN.md, Ghosts): the model and the Npc row the module draws other players with.

PlayerGhost.bfres is Link's model (Playerlink.bfres) made for a ghost: the albedo textures of the materials a ghost
shows as BC3 with a constant alpha (their colour blocks kept) in render pass 1, so the module's alpha-to-coverage
dithers them in screen space; the textures a ghost never shows shrunk to 4x4 to make room. Building it takes native
packages, so it is an asset (ap_assets.py).

The row APGhost is NpcGhost's row drawing that model with Link's animations, with no collision, action, event
triggers or conditions. The module spawns ghosts as NpcGhost actors that read the APGhost row. It is copied from
the game's NpcGhost row before the Ammo Ghost option (ap_ghost.py) rewrites that one.
"""
import io
import struct

SOURCE = "Playerlink.bfres"
FILE_NAME = "PlayerGhost.bfres"
MODEL_NAME = "link"
ROW = "APGhost"
FROM_ROW = "NpcGhost"
ALPHA = 128
# Albedo textures of the shown materials, made BC3 with ALPHA
ALPHA_TEXTURES = ("MI_body_alb.0", "MI_hair_alb", "MI_shieldA_alb", "MI_sowrdA_alb")
ALPHA_PREFIXES = ("MI_ear_alb", "MI_eye_alb", "MI_mouth_alb")
# Material names in texture names: their other textures (normal, mask maps) stay as they are
SHOWN_TEXTURE_PARTS = ("MI_body", "MI_ear", "MI_eye", "MI_mouth", "MI_hair", "MI_shieldA", "MI_sowrdA", "MI_boots")
# The materials drawn in render pass 1 (the module's kShownMaterials)
SHOWN_MATERIALS = ("MI_body", "MI_boots", "MI_ear", "MI_eye", "MI_mouth", "MI_hair", "linkShieldA_MI_shieldA",
                   "linkSwordA_MI_sowrdA", "linkSwordA_MI_sowrdAball")
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
        if alpha and texture.format_ in (0x1a06, 0x1a01):
            levels, _, _ = textures.rawData(texture)
            converted = dds(texture.width, texture.height, b"DXT5", [bc1_to_bc3(bytes(l), ALPHA) for l in levels])
            new = textures.replace(texture, texture.tileMode, texture.format_ == 0x1a06, False, False, True, converted)
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
    graphics["facial"] = ""
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


def add(files) -> None:
    """At patch time, before ap_ghost.add: the APGhost row in the mod's Npc.gsheet."""
    sheet = files.readFile("Npc.gsheet")
    add_row(sheet)
    files.writeFile("Npc.gsheet", sheet)
