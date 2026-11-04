"""The chests' textures (DESIGN.md, Assets), made by the patcher from the player's ObjTreasureBox.bfres: the plain
chest with its albedo recoloured. The constants below are the recipes; the assets hold no texture data
(ap_assets.MADE). The recolours work on the BC1 blocks as they are, fitting each block's two endpoint colours (least
squares over its pixels) to what its pixels become.

ObjTreasureBoxSeashell.bfres: the gold trim turns blue and the red wood dark blue, the purple keeps its colour (a hue
map). Each block keeps its indices, and its endpoints are fitted to its four palette colours recoloured, so a block
whose palette crosses from one hue range into another keeps its in-between colours.

LASR's chests for "Chest Types" (Junk, Key, LifeUpgrade): each part of the chest takes the colour of LASR's flat
texture for it, with the original's shading. The parts are the metal (the metalness map), the wood and the purple
panels (the rest, by hue). LASR's textures are linear BC1, so the game reads their colours as linear light; each part
here takes that colour on average in linear light, its pixels lighter or darker as in the original, in the chest's
own sRGB format. A block crossing parts is fitted to its pixels' own colours and its indices chosen again.
"""
import colorsys
import struct
from functools import lru_cache
from typing import Dict, List, Optional, Sequence, Tuple

import ap_bntx

FILE_NAME = "ObjTreasureBoxSeashell.bfres"
ALBEDO = "MI_dungeonTreasureBox_01_alb"
METALNESS = "MI_dungeonTreasureBox_01_mtl"
BC1 = (0x1a06, 0x1a01)
BC4 = 0x1d01
# LASR's chest looks: per file the colour of the metal, the wood and the purple panels (LASR's flat albedos, linear)
LASR_LOOKS = {"ObjTreasureBoxJunk.bfres": ((104, 105, 104), (107, 69, 24), (49, 30, 13)),
              "ObjTreasureBoxKey.bfres": ((115, 117, 115), (115, 44, 41), (41, 40, 41)),
              "ObjTreasureBoxLifeUpgrade.bfres": ((183, 78, 167), (200, 135, 192), (104, 25, 95))}
METAL, WOOD, PURPLE = range(3)  # the parts
METALLIC = 128  # metalness from which a pixel is metal
NEUTRAL = 0.15  # saturation below which a pixel is no part and keeps its colour (black gaps, grey bolts)
PURPLE_HUES = (200, 320)  # degrees: the purple panels among the non-metal pixels, the rest is wood
SHADING = 0.6  # the power of a pixel's brightness against its part's that the recolour keeps (1: all of it)
# Weight of colour 0 in each palette entry of a 4-colour BC1 block (entries 2 and 3 are the 2/3 and 1/3 mixes)
WEIGHTS = (1.0, 0.0, 2 / 3, 1 / 3)
Colour = Tuple[float, float, float]


def blue_hue(degrees: float) -> float:
    """The recolour's hue map, in degrees: gold (15-70) to blue around 210, red (320-15) to blue around 228, the rest
    kept."""
    if 15 <= degrees < 70:
        return 210 + (degrees - 40) * 0.4
    if degrees < 15 or degrees >= 320:
        return 228 + ((degrees + 40) % 360 - 40) * 0.4
    return degrees


def recolour(colour: Sequence[float]) -> Colour:
    h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in colour))
    r, g, b = colorsys.hsv_to_rgb(blue_hue(h * 360) % 360 / 360, s, v)
    return r * 255, g * 255, b * 255


def from565(c: int) -> Tuple[int, int, int]:
    r, g, b = c >> 11, (c >> 5) & 63, c & 31
    return r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2


def to565(colour: Sequence[float]) -> int:
    r, g, b = (min(255.0, max(0.0, c)) for c in colour)
    return round(r * 31 / 255) << 11 | round(g * 63 / 255) << 5 | round(b * 31 / 255)


def fit(counts: List[int], targets: List[Colour]) -> Tuple[Colour, Colour]:
    """The endpoint colours whose palette is closest (least squares) to targets, each entry weighted by how many
    pixels use it."""
    aa = sum(n * w * w for n, w in zip(counts, WEIGHTS))
    bb = sum(n * (1 - w) * (1 - w) for n, w in zip(counts, WEIGHTS))
    ab = sum(n * w * (1 - w) for n, w in zip(counts, WEIGHTS))
    det = aa * bb - ab * ab
    if det < 1e-9:  # the pixels use colour 0 or colour 1 only (or the mixes in a ratio that fixes neither end)
        target = targets[max(range(4), key=lambda i: counts[i])]
        return target, target
    ends = []
    for channel in range(3):
        ra = sum(n * w * t[channel] for n, w, t in zip(counts, WEIGHTS, targets))
        rb = sum(n * (1 - w) * t[channel] for n, w, t in zip(counts, WEIGHTS, targets))
        ends.append(((ra * bb - rb * ab) / det, (rb * aa - ra * ab) / det))
    return tuple(e[0] for e in ends), tuple(e[1] for e in ends)


def recolour_block(c0: int, c1: int, bits: int) -> bytes:
    if c0 <= c1:  # 3-colour block (with transparent black): its endpoints recoloured, still in 3-colour mode
        n0, n1 = to565(recolour(from565(c0))), to565(recolour(from565(c1)))
        if n0 > n1:  # swap the endpoints and indices 0<->1
            n0, n1 = n1, n0
            bits ^= sum(1 << (2 * p) for p in range(16) if not (bits >> (2 * p)) & 2)
        return struct.pack("<HHI", n0, n1, bits)
    a, b = from565(c0), from565(c1)
    targets = [recolour(tuple(w * x + (1 - w) * y for x, y in zip(a, b))) for w in WEIGHTS]
    counts = [0] * 4
    for p in range(16):
        counts[(bits >> (2 * p)) & 3] += 1
    end0, end1 = fit(counts, targets)
    n0, n1 = to565(end0), to565(end1)
    if n0 < n1:  # 4-colour mode needs n0 > n1: swap the endpoints and indices 0<->1, 2<->3
        n0, n1, bits = n1, n0, bits ^ 0x55555555
    elif n0 == n1:
        bits = 0
    return struct.pack("<HHI", n0, n1, bits)


def recolour_blocks(blocks: bytes) -> bytes:
    return b"".join(recolour_block(*struct.unpack_from("<HHI", blocks, k)) for k in range(0, len(blocks), 8))


def build(source: bytes) -> bytes:
    """ObjTreasureBoxSeashell.bfres from ObjTreasureBox.bfres."""
    bntx, at, size = ap_bntx.open_bfres(source)
    texture = bntx.texture(ALBEDO)
    if texture.format not in BC1:
        raise ValueError(f"{ALBEDO} is not BC1")
    texture.replace(texture.format, texture.width, texture.height,
                    [recolour_blocks(level) for level in texture.levels()])
    return bytes(ap_bntx.replace_bntx(source, bntx.pack(), at, size))


# ---- LASR's chest looks ----

def to_linear(c: float) -> float:
    """An sRGB value (0-1) in linear light."""
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def to_srgb(c: float) -> float:
    c = min(1.0, max(0.0, c))
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def luminance(colour: Sequence[float]) -> float:
    r, g, b = (to_linear(c / 255) for c in colour)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def palette(c0: int, c1: int) -> List[Colour]:
    a, b = from565(c0), from565(c1)
    if c0 > c1:
        return [a, b] + [tuple(w * x + (1 - w) * y for x, y in zip(a, b)) for w in WEIGHTS[2:]]
    return [a, b, tuple((x + y) / 2 for x, y in zip(a, b)), (0, 0, 0)]


def bc4_values(block: bytes) -> List[int]:
    v0, v1 = block[0], block[1]
    if v0 > v1:
        values = [v0, v1] + [((7 - i) * v0 + i * v1) // 7 for i in range(1, 7)]
    else:
        values = [v0, v1] + [((5 - i) * v0 + i * v1) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(block[2:8], "little")
    return [values[(bits >> (3 * p)) & 7] for p in range(16)]


@lru_cache(maxsize=4096)
def part(colour: Colour, metal: bool) -> Optional[int]:
    h, s, _ = colorsys.rgb_to_hsv(*(c / 255 for c in colour))
    if s < NEUTRAL:
        return None
    if metal:
        return METAL
    return PURPLE if PURPLE_HUES[0] <= h * 360 < PURPLE_HUES[1] else WOOD


# A block of the albedo: its endpoints and indices, and each pixel's colour and part
Block = Tuple[int, int, int, List[Colour], List[Optional[int]]]


@lru_cache(maxsize=1)
def chest_parts(source: bytes) -> Tuple[List[List[Block]], Tuple[float, ...]]:
    """The albedo's blocks per mip level with their pixels' parts, and per part the mean of its pixels' luminance to
    the power SHADING (on the top level)."""
    bntx, _, _ = ap_bntx.open_bfres(source)
    albedo, metalness = bntx.texture(ALBEDO), bntx.texture(METALNESS)
    if albedo.format not in BC1 or metalness.format != BC4 or (metalness.width, metalness.height, metalness.mips) != (
            albedo.width, albedo.height, albedo.mips):
        raise ValueError("the chest's albedo and metalness maps are not BC1 and BC4 of one size")
    levels = []
    for colours, metals in zip(albedo.levels(), metalness.levels()):
        blocks = []
        for k in range(0, len(colours), 8):
            c0, c1, bits = struct.unpack_from("<HHI", colours, k)
            entries = palette(c0, c1)
            pixels = [entries[(bits >> (2 * p)) & 3] for p in range(16)]
            metal = bc4_values(metals[k:k + 8])
            blocks.append((c0, c1, bits, pixels, [part(c, m >= METALLIC) for c, m in zip(pixels, metal)]))
        levels.append(blocks)
    sums, counts = [0.0] * 3, [0] * 3
    for _, _, _, pixels, parts in levels[0]:
        for colour, p in zip(pixels, parts):
            if p is not None:
                sums[p] += luminance(colour) ** SHADING
                counts[p] += 1
    return levels, tuple(total / max(1, n) for total, n in zip(sums, counts))


def fit_pixels(indices: List[int], targets: List[Colour]) -> Tuple[Colour, Colour]:
    """The endpoint colours of a 4-colour block whose palette is closest (least squares) to each pixel's target."""
    aa = bb = ab = 0.0
    ra, rb = [0.0] * 3, [0.0] * 3
    for i, t in zip(indices, targets):
        w = WEIGHTS[i]
        aa += w * w
        bb += (1 - w) * (1 - w)
        ab += w * (1 - w)
        for channel in range(3):
            ra[channel] += w * t[channel]
            rb[channel] += (1 - w) * t[channel]
    det = aa * bb - ab * ab
    if det < 1e-9:
        mean = tuple(sum(t[channel] for t in targets) / len(targets) for channel in range(3))
        return mean, mean
    return (tuple((ra[c] * bb - rb[c] * ab) / det for c in range(3)),
            tuple((rb[c] * aa - ra[c] * ab) / det for c in range(3)))


def encode_pixels(targets: List[Colour], indices: List[int]) -> bytes:
    """A 4-colour block for the pixels' targets: endpoints fitted to them, indices chosen again, endpoints fitted
    again."""
    n0 = n1 = 0
    for _ in range(2):
        n0, n1 = (to565(end) for end in fit_pixels(indices, targets))
        if n0 < n1:
            n0, n1 = n1, n0
        if n0 == n1:
            return struct.pack("<HHI", n0, n1, 0)
        entries = palette(n0, n1)
        indices = [min(range(4), key=lambda k: sum((entries[k][c] - t[c]) ** 2 for c in range(3))) for t in targets]
    return struct.pack("<HHI", n0, n1, sum(i << (2 * p) for p, i in enumerate(indices)))


def look_block(block: Block, target) -> bytes:
    c0, c1, bits, pixels, parts = block
    if all(p is None for p in parts):
        return struct.pack("<HHI", c0, c1, bits)
    if c0 > c1 and all(p == parts[0] for p in parts):  # one part: keep the indices, fit to the palette's targets
        counts = [0] * 4
        for p in range(16):
            counts[(bits >> (2 * p)) & 3] += 1
        end0, end1 = fit(counts, [target(c, parts[0]) for c in palette(c0, c1)])
        n0, n1 = to565(end0), to565(end1)
        if n0 < n1:
            n0, n1, bits = n1, n0, bits ^ 0x55555555
        elif n0 == n1:
            bits = 0
        return struct.pack("<HHI", n0, n1, bits)
    indices = [(bits >> (2 * p)) & 3 for p in range(16)]
    if c0 <= c1:  # from a 3-colour block: start from its endpoints
        indices = [i if i < 2 else 0 for i in indices]
    return encode_pixels([target(c, p) for c, p in zip(pixels, parts)], indices)


def build_look(file_name: str, source: bytes) -> bytes:
    """One of LASR's chests (LASR_LOOKS) from ObjTreasureBox.bfres."""
    levels, means = chest_parts(source)
    colours = [tuple(c / 255 for c in colour) for colour in LASR_LOOKS[file_name]]  # linear light, as the game reads them
    targets: Dict[Tuple[Colour, int], Colour] = {}

    def target(colour: Colour, p: Optional[int]) -> Colour:
        if p is None:
            return colour
        key = (colour, p)
        if key not in targets:
            scale = luminance(colour) ** SHADING / means[p]
            targets[key] = tuple(255 * to_srgb(c * scale) for c in colours[p])
        return targets[key]

    bntx, at, size = ap_bntx.open_bfres(source)
    texture = bntx.texture(ALBEDO)
    texture.replace(texture.format, texture.width, texture.height,
                    [b"".join(look_block(block, target) for block in blocks) for blocks in levels])
    return bytes(ap_bntx.replace_bntx(source, bntx.pack(), at, size))
