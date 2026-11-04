"""The multiplayer ghosts' model (DESIGN.md, Ghosts), made by the patcher from the player's Playerlink.bfres.

PlayerGhost.bfres is Link's model made for a ghost: the albedo textures of the materials a ghost shows (Link and the
items on his model: the module shows those its player shows) as BC3 with a constant alpha (their colour blocks kept)
in render pass 1, so the module's alpha-to-coverage dithers them in screen space; the red and blue tunics at half
size and the items' other maps (normal, smoothness, metalness; the mirror shield keeps its, without them it draws
black) and the textures of the items that are other models shrunk to 4x4 to make room. The constants below are the
recipe; the asset holds no texture data (ap_assets.MADE).
"""
import struct
from typing import List

import ap_bntx

FILE_NAME = "PlayerGhost.bfres"
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
# Texture formats: BC1 and BC3 in sRGB (low byte 6) or linear (1); the shrunk textures keep theirs (also linear BC4,
# BC5 and signed BC5)
BC1 = (0x1a06, 0x1a01)
BC3 = (0x1c06, 0x1c01)
SHRINKABLE = BC1 + BC3 + (0x1d01, 0x1e01, 0x1e02)


def bc1_to_bc3(blocks: bytes, alpha: int) -> bytes:
    """BC1 blocks as BC3 blocks with a constant alpha. BC3 reads every colour block in 4-colour mode, so 3-colour
    blocks are rewritten: the half mix becomes the 2/3 mix and transparent black the darker end."""
    alpha_block = bytes((alpha, alpha, 0, 0, 0, 0, 0, 0))
    out: List[bytes] = []
    for k in range(0, len(blocks), 8):
        block = blocks[k:k + 8]
        c0, c1, bits = struct.unpack("<HHI", block)
        if c0 > c1:
            out += (alpha_block, block)
            continue
        dark = 0 if c0 < c1 else 1
        new_bits = 0
        for p in range(16):
            index = (bits >> (2 * p)) & 3
            new_bits |= (dark if index == 3 else index) << (2 * p)
        if c0 == c1:
            new_bits = 0
        if c0 < c1:  # 4-colour mode needs c0 > c1: swap the endpoints and indices 0<->1, 2<->3
            c0, c1 = c1, c0
            new_bits ^= 0x55555555
        elif c0 > 0:
            c1 = c0 - 1
        out += (alpha_block, struct.pack("<HHI", c0, c1, new_bits))
    return b"".join(out)


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


def build(source: bytes) -> bytes:
    """PlayerGhost.bfres from Playerlink.bfres."""
    bntx, at, size = ap_bntx.open_bfres(source)
    for texture in bntx.textures:
        name, format_ = texture.name, texture.format
        alpha = name in ALPHA_TEXTURES or name.startswith(ALPHA_PREFIXES)
        shown = any(name.startswith(m + "_") for m in SHOWN_TEXTURE_PARTS) and not name.startswith("MI_body_alb")
        if alpha and format_ in BC1 + BC3:
            levels, width, height = texture.levels(), texture.width, texture.height
            if name in HALF_TEXTURES:
                levels, width, height = levels[1:], width // 2, height // 2
            convert = bc1_to_bc3 if format_ in BC1 else bc3_scale_alpha
            texture.replace(BC3[BC1.index(format_)] if format_ in BC1 else format_, width, height,
                            [convert(level, ALPHA) for level in levels])
        elif not alpha and not shown and format_ in SHRINKABLE:  # one 4x4 block of zeros
            texture.replace(format_, 4, 4, [bytes(ap_bntx.BLOCK_BYTES[format_ >> 8])])
    raw = ap_bntx.replace_bntx(source, bntx.pack(), at, size)
    if set_render_pass(raw, SHOWN_MATERIALS, 1) == 0:
        raise ValueError("no render pass option found")
    return bytes(raw)
