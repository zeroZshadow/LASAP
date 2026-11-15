"""Universal Tracker's map images from the player's RomFS (DESIGN.md, Universal Tracker map): the island, the dungeons
and the item icons, as PNGs in a zip shaped like a PopTracker pack (images/maps, images/items). The world's tracker
JSON (scripts/ut_map.py) names these files and places its pins by this module's layout. Standard library only.

- The island: the pause map's 23 pieces (FldChip_NN in region_common/ui/Game.arc), each at its part pane's centre
  in the map's layout (blyt/Gm_Map.bflyt, L_FldArea_NN: layout units, y up, 2/3 of a unit per texture pixel), on a
  canvas whose top left is ISLAND_ORIGIN in texture pixels from the layout's centre. A piece is a rectangle of the
  map around its area; the game draws it through its underlay's alpha (FldChipUnderlay_NN, the unexplored look of the
  area, in the same material: Gm_FldChip.bflyt), which cuts out the area, and the island's torn edge with it.
- A dungeon: its rooms' map textures (Lv<NN><Name>_<row><column> in DgnTex.arc, ROOM_PIXELS each) on the game's map
  grid (DgnMapGrid_00), the first room's top left at GRID_ORIGIN. A room is ROOM_UNITS of the level's world units.
  DgnTex.arc also has textures for cells the level has no room in (Face Shrine's 02C to 02F) and for rooms in two
  variants (<row><column>L and U: side views, Eagle's Tower's 05E), which the map does not show: a texture is drawn
  only when the level has a room of its name (level_cells), as the game's UiDungeonArea datasheet lists them.
- Both at half size (STEP): ASTC decodes only the texels kept (ap_astc), the slow part, spread over processes.
- The item icons (ITEM_ICONS) at full size, from Game.arc, trimmed to what they draw (trim): Universal Tracker fits
  an icon to its cell, and the game's icons sit in margins of different sizes.
- A dungeon's stair letters (the world's tracker/maps.json "stairs", by image): a white disc with the letter at each
  stair (draw_letter), from FONT.
"""
import hashlib
import json
import os
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple, Union

import ap_astc
import ap_bntx

# Bump when the images change, so clients build them again (a change of the stair letters renames the zip: romfs_key)
MAP_VERSION = 5
UI = Path("region_common", "ui")
LEVEL = Path("region_common", "level")
STEP = 2  # the maps' scale: texture pixels per image pixel
ISLAND_PIECES = 23
ISLAND_ORIGIN = (-2048, -1063)  # the island canvas's top left, in texture pixels from the layout's centre
ISLAND_SIZE = (4107, 2299)  # in texture pixels
LAYOUT_PIXELS = 1.5  # texture pixels per layout unit
ROOM_PIXELS = (160, 128)
ROOM_UNITS = (15.0, 12.0)
GRID_ORIGIN = (14, 8)  # the first room's top left on DgnMapGrid_00 (1308 x 1040 around 8 x 8 rooms)
# The pack-shaped names: dungeon number -> level name prefix of its rooms
LEVELS = {1: "Lv01TailCave", 2: "Lv02BottleGrotto", 3: "Lv03KeyCavern", 4: "Lv04AnglersTunnel", 5: "Lv05CatfishsMaw",
          6: "Lv06FaceShrine", 7: "Lv07EagleTower", 8: "Lv08TurtleRock", 0: "Lv10ClothesDungeon"}
ISLAND_IMAGE = "images/maps/island.png"
# A dungeon map's stair letters: their discs' size in image pixels, and each letter's 5 x 7 glyph, rows from the top
LETTER_DISC = 16
FONT = {
    "A": (".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"),
    "B": ("####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."),
    "C": (".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."),
    "D": ("####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."),
    "E": ("#####", "#....", "#....", "####.", "#....", "#....", "#####"),
    "F": ("#####", "#....", "#....", "####.", "#....", "#....", "#...."),
    "G": (".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"),
    "H": ("#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"),
    "J": ("..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."),
    "K": ("#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"),
    "L": ("#....", "#....", "#....", "#....", "#....", "#....", "#####"),
    "M": ("#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"),
    "N": ("#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"),
    "P": ("####.", "#...#", "#...#", "####.", "#....", "#....", "#...."),
}
LETTERS = "".join(FONT)  # in the order they are given out (I and O left out: they read as 1 and 0)
# Format suffixes of Game.arc's textures, in the order an icon takes them: BC3, then ASTC
ICON_SUFFIXES = ("^q", "^w", "^_A", "^_D")
TRIM_ALPHA = 16  # an icon's texels with no more alpha than this are trimmed off (trim): faint glow
_INSTRUMENTS = ("full-moon-cello", "conch-horn", "sea-lilys-bell", "surf-harp", "wind-marimba", "coral-triangle",
                "evening-calm-organ", "thunder-drum")
_KEYS = ("tail-key", "slime-key", "angler-key", "face-key", "bird-key")
# LASR item (dungeon items without their -D<n>) -> its icon's texture in Game.arc, without the format suffix; a
# progressive item has one per level. The level 1 sword's icon (EqSword_00) is BC7 only, which nothing here decodes:
# both levels show the level 2 sword. Bow-Wow, the rooster, the shell sensor and the golden leaf have no icon.
ITEM_ICONS: Dict[str, Union[str, List[str]]] = {
    "sword": ["EqSword_01", "EqSword_01"], "shield": ["EqShield_00", "EqShield_01"],
    "bracelet": ["EqPowerBracelet_00", "EqPowerBracelet_01"], "boots": "EqPegasusBoots_00",
    "flippers": "Eqflippers_00", "feather": "ItemRocsFeather_00", "bomb": "ItemBomb_00", "shovel": "ItemShovel_00",
    "ocarina": "ItemOcarina_03", "hookshot": "ItemFookShot_00", "bow": "ItemBow_00", "rod": "ItemMagicRod_00",
    "boomerang": "ItemBoomerang_00", "powder": "ItemMagicPowder_00", "mushroom": "ItemMushroom_00",
    "bottle": "ItemBottle_00", "red-tunic": "EqCloth_01", "blue-tunic": "EqCloth_02",
    "seashell": "CollectSecretSeashell_00", "medicine": "CollectSecretMedicine_00",
    "song-ballad": "ItemMusic00", "song-mambo": "ItemMusic01", "song-soul": "ItemMusic02",
    **{item: f"CollectInst_{i:02d}" for i, item in enumerate(_INSTRUMENTS)},
    **{item: f"CollectDgnKey_{i:02d}" for i, item in enumerate(_KEYS)},
    "yoshi-doll": "TrdYoshiDoll_00", "ribbon": "TrdRibbon_00", "dog-food": "TrdDogfood_00", "bananas": "TrdBananas_00",
    "stick": "TrdStick_00", "honeycomb": "TrdHoneycomb_00", "pineapple": "TrdPineapple_00",
    "hibiscus": "TrdHibiscus_00", "letter": "TrdLetter_00", "broom": "TrdBroom_00", "fishing-hook": "TrdFishingHook_00",
    "necklace": "TrdNecklace_00", "mermaid-scale": "TrdScale_00", "magnifying-lens": "TrdLens_00",
    "key": "SmallKey_00", "nightmare-key": "BossKey_00", "stone-beak": "DgnBeak_00", "map": "DgnMap_00",
    "compass": "DgnCompass_00",
}


def dungeon_image(number: int) -> str:
    return f"images/maps/d{number}.png"


def icon_image(texture: str) -> str:
    return f"images/items/{texture}.png"


def scaled(full: int) -> int:
    """An image size or position at STEP, from texture pixels."""
    return -(-full // STEP)


# --- reading the RomFS

def read_sarc(data: bytes) -> Dict[str, bytes]:
    """An uncompressed SARC archive's files by name."""
    if data[:4] != b"SARC" or struct.unpack_from("<H", data, 6)[0] != 0xfeff:
        raise ValueError("not a little-endian SARC archive")
    header, data_start = struct.unpack_from("<H", data, 4)[0], struct.unpack_from("<I", data, 12)[0]
    count = struct.unpack_from("<H", data, header + 6)[0]
    nodes = header + 12
    names_at = nodes + 16 * count + 8  # after the SFNT header
    files = {}
    for i in range(count):
        _, attributes, start, end = struct.unpack_from("<4I", data, nodes + 16 * i)
        if attributes >> 24:
            at = names_at + (attributes & 0xffff) * 4
            name = data[at:data.index(b"\0", at)].decode("utf-8")
            files[name] = data[data_start + start:data_start + end]
    return files


def pane_centres(layout: bytes, prefix: str) -> Dict[str, Tuple[float, float]]:
    """The translation of each picture or part pane of a BFLYT layout whose name starts with prefix."""
    if layout[:4] != b"FLYT":
        raise ValueError("not a BFLYT layout")
    pos, sections = struct.unpack_from("<H", layout, 6)[0], struct.unpack_from("<H", layout, 0x10)[0]
    centres = {}
    for _ in range(sections):
        kind, size = struct.unpack_from("<4sI", layout, pos)
        if kind in (b"pic1", b"prt1"):
            name = layout[pos + 12:pos + 36].split(b"\0")[0].decode("utf-8")
            if name.startswith(prefix):
                centres[name] = struct.unpack_from("<2f", layout, pos + 44)
        pos += size
    return centres


def level_cells(romfs: Path, level: str) -> set:
    """The map cells ("02C") the level has a room of that name in: its rooms' files, <level>_<row><column>.leb."""
    return {m[1] for f in (romfs / LEVEL / level).iterdir()
            if (m := re.fullmatch(re.escape(level) + r"_(\d\d[A-H])\.leb", f.name))}


def textures(archive: Dict[str, bytes]) -> Dict[str, ap_bntx.Texture]:
    return {t.name: t for t in ap_bntx.Bntx(archive["timg/__Combined.bntx"]).textures}


def astc_job(texture: ap_bntx.Texture, step: int) -> tuple:
    bw, bh, _ = texture.block_size()
    return texture.level0(), texture.width, texture.height, bw, bh, step


def decode_job(job: tuple) -> Tuple[bytes, int, int]:
    """RGBA, width and height of an ASTC texture at its step, cropped to its own size (scaled)."""
    blocks, width, height, bw, bh, step = job
    rgba, w, h = ap_astc.decode(blocks, width, height, bw, bh, step)
    return crop(rgba, w, h, -(-width // step), -(-height // step)), -(-width // step), -(-height // step)


def crop(rgba: bytes, width: int, height: int, to_width: int, to_height: int) -> bytes:
    if (to_width, to_height) == (width, height):
        return rgba
    return b"".join(rgba[y * width * 4:(y * width + to_width) * 4] for y in range(to_height))


def with_alpha(rgba: bytes, width: int, height: int, mask: bytes, mask_width: int, mask_height: int) -> bytes:
    """The image with the mask's alpha (both from the top left; clear outside the mask)."""
    out = bytearray(rgba)
    out[3::4] = bytes(width * height)
    w = min(width, mask_width)
    for y in range(min(height, mask_height)):
        out[y * width * 4 + 3:(y * width + w) * 4:4] = mask[y * mask_width * 4 + 3:(y * mask_width + w) * 4:4]
    return bytes(out)


def trim(rgba: bytes, width: int, height: int) -> Tuple[bytes, int, int]:
    """The image cut to the box of its texels with more than TRIM_ALPHA alpha (all of it when there are none)."""
    alpha = rgba[3::4]
    rows = [y for y in range(height) if max(alpha[y * width:(y + 1) * width]) > TRIM_ALPHA]
    columns = [x for x in range(width) if max(alpha[x::width]) > TRIM_ALPHA]
    if not rows:
        return rgba, width, height
    x0, x1, y0, y1 = columns[0], columns[-1] + 1, rows[0], rows[-1] + 1
    out = b"".join(rgba[(y * width + x0) * 4:(y * width + x1) * 4] for y in range(y0, y1))
    return out, x1 - x0, y1 - y0


def icon(texture: ap_bntx.Texture) -> Tuple[bytes, int, int]:
    kind = texture.format >> 8
    if kind == 0x1c:
        rgba = ap_bntx.decode_bc3(texture.level0(), texture.width, texture.height)
        w, h = -(-texture.width // 4) * 4, -(-texture.height // 4) * 4
        return trim(crop(rgba, w, h, texture.width, texture.height), texture.width, texture.height)
    return trim(*decode_job(astc_job(texture, 1)))


def icon_texture(found: Dict[str, ap_bntx.Texture], base: str) -> ap_bntx.Texture:
    for suffix in ICON_SUFFIXES:
        if base + suffix in found:
            return found[base + suffix]
    raise KeyError(f"Game.arc has no icon {base}")


# --- images

def composite(canvas: bytearray, width: int, height: int, image: bytes, iw: int, ih: int, x: int, y: int) -> None:
    """Draws an RGBA image over the canvas with its top left at (x, y): opaque runs are copied, partly transparent
    texels blended (straight alpha), clear ones skipped."""
    for row in range(max(0, -y), min(ih, height - y)):
        line = image[row * iw * 4:(row + 1) * iw * 4]
        alpha = line[3::4]
        x0, x1 = max(0, -x), min(iw, width - x)
        base = ((y + row) * width + x) * 4
        for m in re.finditer(rb"[^\x00]+", alpha[x0:x1]):
            start, end = m.start() + x0, m.end() + x0
            run = alpha[start:end]
            for solid in re.finditer(rb"\xff+|[^\xff]+", run):
                s, e = solid.start() + start, solid.end() + start
                if run[solid.start()] == 255:
                    canvas[base + s * 4:base + e * 4] = line[s * 4:e * 4]
                    continue
                for px in range(s, e):
                    a = line[px * 4 + 3] * 255  # the source's weight and the canvas's, of 255 * 255
                    o = base + px * 4
                    below = canvas[o + 3] * (255 - line[px * 4 + 3])
                    total = a + below
                    if total:
                        for ch in range(3):
                            canvas[o + ch] = (line[px * 4 + ch] * a + canvas[o + ch] * below) // total
                    canvas[o + 3] = total // 255


def letter_image(letter: str, size: int = LETTER_DISC, samples: int = 4) -> bytes:
    """A stair letter's RGBA image, size x size: a white disc with a dark rim and the letter in black (FONT, bold: each
    glyph cell inks the one right of it too), each pixel the mean of samples x samples points."""
    glyph, radius = FONT[letter], size / 2
    width = len(glyph[0]) + 1
    cell = size * 0.62 / len(glyph)  # the glyph's height is 0.62 of the disc's
    left, top = radius - cell * width / 2, radius - cell * len(glyph) / 2
    out = bytearray(size * size * 4)
    for py in range(size):
        for px in range(size):
            shade = alpha = 0
            for sy in range(samples):
                for sx in range(samples):
                    x, y = px + (sx + 0.5) / samples, py + (sy + 0.5) / samples
                    d = ((x - radius) ** 2 + (y - radius) ** 2) ** 0.5
                    if d > radius:
                        continue
                    gx, gy = int((x - left) // cell), int((y - top) // cell)
                    row = glyph[gy] if 0 <= gy < len(glyph) else ""
                    inked = d > radius - 1 or "#" in row[max(0, gx - 1):max(0, gx + 1)]
                    shade += 0x20 if inked else 0xff
                    alpha += 1
            o = (py * size + px) * 4
            if alpha:
                out[o:o + 3] = bytes([shade // alpha] * 3)
                out[o + 3] = alpha * 255 // (samples * samples)
    return bytes(out)


def draw_letters(canvas: bytearray, width: int, height: int, stairs: Sequence[dict]) -> None:
    """A dungeon image's stair letters (maps.json's "stairs"): each disc centred on its stair."""
    images: Dict[str, bytes] = {}
    for stair in stairs:
        image = images.setdefault(stair["letter"], letter_image(stair["letter"]))
        composite(canvas, width, height, image, LETTER_DISC, LETTER_DISC, round(stair["x"] - LETTER_DISC / 2),
                  round(stair["y"] - LETTER_DISC / 2))


def png(rgba: bytes, width: int, height: int) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    stride = width * 4
    raw = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def romfs_key(romfs: Path, stairs: Optional[Dict[str, list]] = None) -> str:
    """Names what the images are built from: the two archives, by their sizes and modification times (reading Game.arc
    whole would take seconds on a slow disk), the stair letters and MAP_VERSION."""
    digest = hashlib.sha1()
    for name in ("Game.arc", "DgnTex.arc"):
        stat = (romfs / UI / name).stat()
        digest.update(f"{name} {stat.st_size} {stat.st_mtime_ns}\n".encode())
    if stairs:
        digest.update(json.dumps(stairs, sort_keys=True).encode())
    return f"{digest.hexdigest()[:16]}-v{MAP_VERSION}"


def decode_all(jobs: Sequence[tuple], processes: int, path_entry: Optional[str]) -> List[Tuple[bytes, int, int]]:
    """The ASTC jobs decoded, over a process pool when processes > 1 (its workers import this module from
    path_entry); in this process when that fails."""
    if processes > 1:
        try:
            from concurrent.futures import ProcessPoolExecutor
            init = f"import sys; sys.path.insert(0, {path_entry!r})" if path_entry else "pass"
            order = sorted(range(len(jobs)), key=lambda i: -len(jobs[i][0]))  # big ones first
            with ProcessPoolExecutor(processes, initializer=exec, initargs=(init,)) as pool:
                done = dict(zip(order, pool.map(decode_job, [jobs[i] for i in order], chunksize=1)))
            return [done[i] for i in range(len(jobs))]
        except Exception as e:  # no processes here: decode in this one
            print(f"Decoding in one process ({e})", file=sys.stderr)
    return [decode_job(job) for job in jobs]


def stair_letters(maps: Sequence[dict]) -> Dict[str, list]:
    """The world's tracker/maps.json's stair letters, by image."""
    return {m["img"]: m["stairs"] for m in maps if m.get("stairs")}


def build(romfs: Path, out: Path, processes: Optional[int] = None, path_entry: Optional[str] = None,
          stairs: Optional[Dict[str, list]] = None) -> None:
    """Writes the tracker's zip from the RomFS to out (through a temporary file next to it), with the dungeons' stair
    letters (stair_letters)."""
    game = read_sarc((romfs / UI / "Game.arc").read_bytes())
    found = textures(game)
    rooms = textures(read_sarc((romfs / UI / "DgnTex.arc").read_bytes()))
    centres = pane_centres(game["blyt/Gm_Map.bflyt"], "L_FldArea_")

    jobs, places = [], []  # ASTC jobs and where each goes: (image, x, y) in texture pixels; "mask" for an underlay
    for i in range(ISLAND_PIECES):
        piece = found[f"FldChip_{i:02d}^_H"]
        cx, cy = centres[f"L_FldArea_{i:02d}"]
        jobs.append(astc_job(piece, STEP))
        places.append((ISLAND_IMAGE, round(cx * LAYOUT_PIXELS - piece.width / 2) - ISLAND_ORIGIN[0],
                       round(-cy * LAYOUT_PIXELS - piece.height / 2) - ISLAND_ORIGIN[1]))
        jobs.append(astc_job(found[f"FldChipUnderlay_{i:02d}^_H"], STEP))
        places.append(("mask", 0, 0))
    grid = found["DgnMapGrid_00^_A"]
    jobs.append(astc_job(grid, STEP))
    places.append(("grid", 0, 0))
    for number, level in LEVELS.items():
        cells = level_cells(romfs, level)
        for name, room in rooms.items():
            m = re.fullmatch(level + r"_(\d\d)([A-H])", name)
            if m and m[1] + m[2] in cells:
                jobs.append(astc_job(room, STEP))
                places.append((dungeon_image(number), GRID_ORIGIN[0] + (ord(m[2]) - 65) * ROOM_PIXELS[0],
                               GRID_ORIGIN[1] + (int(m[1]) - 1) * ROOM_PIXELS[1]))
    decoded = decode_all(jobs, processes if processes is not None else (os.cpu_count() or 1), path_entry)

    files: Dict[str, bytes] = {}
    grid_image = decoded[places.index(("grid", 0, 0))]
    canvases: Dict[str, Tuple[bytearray, int, int]] = {ISLAND_IMAGE: (bytearray(scaled(ISLAND_SIZE[0])
                                                                                 * scaled(ISLAND_SIZE[1]) * 4),
                                                                       scaled(ISLAND_SIZE[0]), scaled(ISLAND_SIZE[1]))}
    for number in LEVELS:
        canvases[dungeon_image(number)] = (bytearray(grid_image[0]), grid_image[1], grid_image[2])
    for i, ((target, x, y), (rgba, w, h)) in enumerate(zip(places, decoded)):
        if target in canvases:
            if i + 1 < len(places) and places[i + 1][0] == "mask":
                rgba = with_alpha(rgba, w, h, *decoded[i + 1])
            canvas, cw, ch = canvases[target]
            composite(canvas, cw, ch, rgba, w, h, x // STEP, y // STEP)
    for name, (canvas, cw, ch) in canvases.items():
        draw_letters(canvas, cw, ch, (stairs or {}).get(name, ()))
        files[name] = png(bytes(canvas), cw, ch)
    for value in ITEM_ICONS.values():
        for base in ([value] if isinstance(value, str) else value):
            if icon_image(base) not in files:
                files[icon_image(base)] = png(*icon(icon_texture(found, base)))

    out.parent.mkdir(parents=True, exist_ok=True)
    temporary = out.with_suffix(".part")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("manifest.json", f'{{"name": "Link\'s Awakening tracker images", "version": {MAP_VERSION}}}')
        for name, data in files.items():
            archive.writestr(name, data)
    os.replace(temporary, out)


def main(argv: Iterable[str]) -> int:
    """Builds the zip from the command line: <RomFS> <out.zip> [<maps.json> [processes]], the stair letters from the
    world's tracker/maps.json."""
    args = list(argv)
    stairs = stair_letters(json.loads(Path(args[2]).read_text(encoding="utf-8"))) if len(args) > 2 else None
    build(Path(args[0]), Path(args[1]), int(args[3]) if len(args) > 3 else None, str(Path(__file__).parent), stairs)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
