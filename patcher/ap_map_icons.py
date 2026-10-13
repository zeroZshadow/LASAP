"""The map tracker's overworld icons (DESIGN.md, Map tracker): dots in PopTracker's colours, green in logic, yellow
partly in logic, red out of logic and grey with a tick once collected, in Game.arc.

The map's marker (Gm_MapMarker) shows the frame of its SetMark animation given by the icon row's mType. That
animation picks one of the marker textures per frame, and sets the marker's size per frame. This adds a texture per
look to the archive's textures and frames after the game's own: per look one at the size of a group of checks, then
per look one at the size of a single check (frame()).

Built once from a RomFS dump, as an asset (ap_assets.py)."""
import io
import math
import struct
from pathlib import Path

ARCHIVE = Path("region_common", "ui", "Game.arc")
ANIMATION = "anim/Gm_MapMarker_SetMark.bflan"
TEXTURES = "timg/__Combined.bntx"
TEMPLATE = "MarkerSecretSeashell_00^w"  # a marker texture; the new ones copy its settings
GAME_FRAMES = 7  # the SetMark frames of the game's markers
# In the order of the looks: in logic, partly in logic, out of logic, collected (ap_map.LOOKS)
NAMES = ("MarkerApInLogic_00^w", "MarkerApPartial_00^w", "MarkerApOutOfLogic_00^w", "MarkerApCollected_00^w")
COLORS = ((72, 184, 84), (236, 196, 52), (214, 72, 60), (150, 150, 150))
OUTLINE = (58, 44, 34)
TICK = (250, 246, 232)
COLLECTED_ALPHA = 0.6
GROUP_SIZE = 18.0   # marker size (layout units; the game's markers are 28 to 32)
SINGLE_SIZE = 14.0
TEXTURE_SIZE = 64
SUPERSAMPLE = 4
DOT_RADIUS = 0.42   # of the texture's width; the outline reaches the edge
OUTLINE_WIDTH = 0.08


def frame(look: int, single: bool) -> int:
    """The SetMark frame (icon row mType) of a look, at the size of a single check or of a group."""
    return GAME_FRAMES + (len(NAMES) if single else 0) + look


# Textures

def _segment_distance(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def _dot_pixels(look: int) -> bytes:
    """RGBA pixels of a look's dot, antialiased by supersampling."""
    n = TEXTURE_SIZE
    center = n / 2
    fill_radius = DOT_RADIUS * n
    outer_radius = (DOT_RADIUS + OUTLINE_WIDTH) * n
    # a tick: two strokes through these points
    tick = [(0.30 * n, 0.52 * n), (0.44 * n, 0.66 * n), (0.70 * n, 0.36 * n)]
    tick_width = 0.065 * n
    color = COLORS[look]
    out = bytearray()
    for y in range(n):
        for x in range(n):
            rgb = [0.0, 0.0, 0.0]
            alpha = 0.0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    px, py = x + (sx + 0.5) / SUPERSAMPLE, y + (sy + 0.5) / SUPERSAMPLE
                    d = math.hypot(px - center, py - center)
                    if d > outer_radius:
                        continue
                    sample = OUTLINE if d > fill_radius else color
                    if look == 3 and min(_segment_distance(px, py, *tick[i], *tick[i + 1])
                                         for i in range(2)) <= tick_width:
                        sample = TICK
                    rgb = [c + s for c, s in zip(rgb, sample)]
                    alpha += 1
            if alpha:
                out += bytes(round(c / alpha) for c in rgb)
            else:
                out += bytes(3)
            coverage = alpha / SUPERSAMPLE ** 2
            out.append(round(255 * coverage * (COLLECTED_ALPHA if look == 3 else 1.0)))
    return bytes(out)


def _dds(pixels: bytes, size: int) -> io.BytesIO:
    """An uncompressed RGBA DDS (byte order R, G, B, A)."""
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<7I", header, 4, 124, 0x100f, size, size, size * 4, 0, 1)
    struct.pack_into("<2I", header, 76, 32, 0x41)  # pixel format: RGB with alpha
    struct.pack_into("<5I", header, 88, 32, 0xff, 0xff00, 0xff0000, 0xff000000)
    struct.pack_into("<I", header, 108, 0x1000)
    return io.BytesIO(bytes(header) + pixels)


def _bit(name: bytes, bit: int) -> int:
    """Bit of a name as the textures' name dictionary counts them: from the name's last byte, low bit first."""
    index = bit >> 3
    return (name[len(name) - 1 - index] >> (bit & 7)) & 1 if index < len(name) else 0


def build_dictionary(names: list) -> list:
    """The nodes (reference bit, left, right, name index or -1) of a name dictionary (_DIC): a Patricia trie with
    a root node, the names inserted in order."""
    keys = [b""] + [name.encode() for name in names]
    nodes = [[-1, 0, 0]]  # the root; it leads left

    def child(index: int, key: bytes) -> int:
        ref, left, right = nodes[index]
        return right if ref >= 0 and _bit(key, ref) else left

    for index in range(1, len(keys)):
        key = keys[index]
        parent, current = 0, nodes[0][1]
        while nodes[parent][0] < nodes[current][0]:
            parent, current = current, child(current, key)
        found = keys[current]
        bit = next(b for b in range(max(len(found), len(key)) * 8) if _bit(found, b) != _bit(key, b))
        parent, current = 0, nodes[0][1]
        while nodes[parent][0] < nodes[current][0] < bit:
            parent, current = current, child(current, key)
        nodes.append([bit, current, index] if _bit(key, bit) else [bit, index, current])
        ref, left, right = nodes[parent]
        if ref >= 0 and _bit(key, ref):
            nodes[parent][2] = index
        else:
            nodes[parent][1] = index
    return [(ref & 0xffffffff, left, right, i - 1) for i, (ref, left, right) in enumerate(nodes)]


def _add_textures(data: bytes) -> bytes:
    import copy
    from RandomizerCore.Tools.bntx_editor import bntx_editor
    from RandomizerCore.Tools.bntx_editor.structs import StringTable

    editor = bntx_editor.BNTXEditor()
    editor.openFile(data)
    bntx = editor.bntx
    dictionary = bntx.texNameDict
    if [(n.referenceBit, n.leftIdx, n.rightIdx) for n in dictionary.entries] != \
            [node[:3] for node in build_dictionary([t.name for t in bntx.textures])]:
        raise ValueError(f"{TEXTURES}: the name dictionary is not built as build_dictionary() builds it")
    template = next(t for t in bntx.textures if t.name == TEMPLATE)
    for look, name in enumerate(NAMES):
        texture = bntx.replace(copy.deepcopy(template), template.tileMode, True, False, False, False,
                               _dds(_dot_pixels(look), TEXTURE_SIZE))
        if not texture:
            raise ValueError(f"cannot make texture {name}")
        entry = StringTable.Entry(bntx.header.endianness)
        entry.string, entry.size_ = name, len(name.encode())
        bntx.strTbl.entries.append(entry)
        bntx.strTbl.count += 1
        texture.name, texture.nameIdx = name, len(bntx.strTbl.entries) - 1
        bntx.textures.append(texture)
    bntx.texContainer.count = len(bntx.textures)
    # The dictionary names the textures in their order.
    nodes = build_dictionary([t.name for t in bntx.textures])
    dictionary.entries = []
    for i, (ref, left, right, name) in enumerate(nodes):
        node = StringTable.TexNameDict.Entry(dictionary.endianness)
        node.referenceBit, node.leftIdx, node.rightIdx = ref, left, right
        node.strIdx = -1 if i == 0 else bntx.textures[name].nameIdx
        dictionary.entries.append(node)
    dictionary.count = len(bntx.textures)
    return bytes(editor.save())


# Animation

class _Animation:
    """The pai1 section of a layout animation (BFLAN): textures and the animated panes and materials."""

    def __init__(self, data: bytes, offset: int):
        self.offset = offset
        self.size = struct.unpack_from("<I", data, offset + 4)[0]
        self.frames, self.flags = struct.unpack_from("<HB", data, offset + 8)
        texture_count, content_count, contents_at = struct.unpack_from("<HHI", data, offset + 0xc)
        names_at = offset + 0x14
        self.textures = []
        for i in range(texture_count):
            start = names_at + struct.unpack_from("<I", data, names_at + 4 * i)[0]
            self.textures.append(data[start:data.index(b"\0", start)].decode())
        self.contents = []
        for i in range(content_count):
            at = offset + struct.unpack_from("<I", data, offset + contents_at + 4 * i)[0]
            name = data[at:at + 28]
            tag_count, kind = struct.unpack_from("<BB", data, at + 28)
            tags = []
            for j in range(tag_count):
                tag_at = at + struct.unpack_from("<I", data, at + 32 + 4 * j)[0]
                magic, target_count = data[tag_at:tag_at + 4], data[tag_at + 4]
                targets = []
                for k in range(target_count):
                    target_at = tag_at + struct.unpack_from("<I", data, tag_at + 8 + 4 * k)[0]
                    index, target, curve, key_count, keys_at = struct.unpack_from("<BBBxHxxI", data, target_at)
                    key_size = 8 if curve == 1 else 12
                    keys_start = target_at + keys_at
                    keys = [data[keys_start + key_size * n:keys_start + key_size * (n + 1)] for n in range(key_count)]
                    targets.append([index, target, curve, keys])
                tags.append([magic, targets])
            self.contents.append([name, kind, tags])

    def write(self) -> bytes:
        out = bytearray(struct.pack("<4sIHBxHHI", b"pai1", 0, self.frames, self.flags, len(self.textures),
                                    len(self.contents), 0))
        names = bytearray()
        offsets = []
        for name in self.textures:
            offsets.append(4 * len(self.textures) + len(names))
            names += name.encode() + b"\0"
        out += b"".join(struct.pack("<I", o) for o in offsets) + names
        out += bytes(-len(out) % 4)
        struct.pack_into("<I", out, 0x10, len(out))
        table_at = len(out)
        out += bytes(4 * len(self.contents))
        for i, (name, kind, tags) in enumerate(self.contents):
            struct.pack_into("<I", out, table_at + 4 * i, len(out))
            content = bytearray(name + struct.pack("<BBxx", len(tags), kind) + bytes(4 * len(tags)))
            for j, (magic, targets) in enumerate(tags):
                struct.pack_into("<I", content, 32 + 4 * j, len(content))
                tag = bytearray(magic + struct.pack("<Bxxx", len(targets)) + bytes(4 * len(targets)))
                for k, (index, target, curve, keys) in enumerate(targets):
                    struct.pack_into("<I", tag, 8 + 4 * k, len(tag))
                    tag += struct.pack("<BBBxHxxI", index, target, curve, len(keys), 12) + b"".join(keys)
                content += tag
            out += content
        struct.pack_into("<I", out, 4, len(out))
        return bytes(out)


def _patch_animation(data: bytes) -> bytes:
    """SetMark with a frame per look and size (frame()): the new textures, and the marker's size (pane size, targets
    8 and 9 of the pane's FLPA tag) set to the game's last size until the new frames."""
    section = struct.unpack_from("<H", data, 6)[0]  # after the header
    while data[section:section + 4] != b"pai1":
        section += struct.unpack_from("<I", data, section + 4)[0]
    animation = _Animation(data, section)
    if animation.frames != GAME_FRAMES or animation.write() != data[section:section + animation.size]:
        raise ValueError(f"{ANIMATION} is not the file of game version 1.0.0")
    first = len(animation.textures)
    animation.textures += NAMES
    animation.frames = frame(len(NAMES) - 1, True) + 1
    for _, _, tags in animation.contents:
        for magic, targets in tags:
            for _, target, curve, keys in targets:
                if magic == b"FLTP" and curve == 1:
                    keys += [struct.pack("<fHxx", frame(look, single), first + look)
                             for single in (False, True) for look in range(len(NAMES))]
                elif magic == b"FLPA" and target in (8, 9):
                    last = struct.unpack_from("<f", keys[-1], 4)[0]
                    for at, before, after in ((frame(0, False), last, GROUP_SIZE),
                                              (frame(0, True), GROUP_SIZE, SINGLE_SIZE)):
                        keys += [struct.pack("<3f", at, before, 0.0), struct.pack("<3f", at, after, 0.0)]
    pai1 = animation.write()
    out = bytearray(data[:section] + pai1 + data[section + animation.size:])
    struct.pack_into("<I", out, 0xc, len(out))
    return bytes(out)


def build(archive: bytes) -> bytes:
    """Game.arc with the map tracker's marker textures and frames."""
    import oead
    sarc = oead.Sarc(archive)
    writer = oead.SarcWriter.from_sarc(sarc)
    writer.set_min_alignment(16)
    writer.add_alignment_requirement("bnsh", 0x1000)
    writer.add_alignment_requirement("bntx", 0x1000)
    writer.files[ANIMATION] = _patch_animation(bytes(sarc.get_file(ANIMATION).data))
    writer.files[TEXTURES] = _add_textures(bytes(sarc.get_file(TEXTURES).data))
    return bytes(writer.write()[1])
