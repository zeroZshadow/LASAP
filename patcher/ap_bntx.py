"""BNTX textures in a bfres model (DESIGN.md, Assets): reading a BNTX's textures, the Tegra X1 block-linear layout
they are stored in, replacing textures and writing the BNTX again. Standard library only, so the patcher can make
textures from the player's RomFS.

A BNTX holds a header, a texture container, the texture info pointers, a string table with the texture name
dictionary, one BRTI block per texture (its info and mip pointers), the BRTD data block and a relocation table. pack()
lays them out in that order from the textures (the container's pointers, the info blocks and the relocation table
follow from them), with the data block and the relocation table aligned to the file's alignment (4 KiB).

Block-linear layout: the surface is a grid of blocks of GOBs. A GOB is 64 bytes wide and 8 rows high (512 bytes); a
block is `block_height` GOBs stacked vertically, and blocks run left to right, then top to bottom. Inside a GOB a row
splits into four 16-byte runs: run x/16 of row y sits at (x % 64 // 32) * 256 + (y % 8 // 2) * 64 + (x % 32 // 16) * 32
+ (y % 2) * 16. A row of a compressed texture is a row of 4x4 pixel blocks. A texture's block height (in GOBs) is the
power of two that covers an eighth of its height in rows, at most 16; each smaller mip level whose rows (rounded up to
a power of two) no longer fill a block halves it.
"""
import struct
from typing import List, Tuple

# Format (high byte) -> bytes per element; compressed formats store 4x4 pixel blocks as elements
BLOCK_BYTES = {0x1a: 8, 0x1b: 16, 0x1c: 16, 0x1d: 8, 0x1e: 16, 0x1f: 16, 0x20: 16}
# Component selection of a texture made from a plain BCn image: RGBA for BC1-3, R as RGB with alpha 1 for BC4, RG
# with blue 0 and alpha 1 for BC5
COMPONENTS = {0x1a: 0x05040302, 0x1b: 0x05040302, 0x1c: 0x05040302, 0x1d: 0x01020202, 0x1e: 0x01000302}
GOB_WIDTH, GOB_ROWS = 64, 8
GOB_BYTES = GOB_WIDTH * GOB_ROWS
ALIGNMENT_SHIFT = 12  # the BNTX's alignment on the Switch: 4 KiB
BLOCK_LINEAR = 0
TEXTURE_ALIGNMENT = 512  # of a block-linear texture's data
INFO_POINTERS = 0x198  # after the header, the container and the memory pool
INFO_BLOCK = 0x2a0  # a BRTI block without its mip pointers: header, info, then room for the GPU's texture objects
ACCESS_TEXTURE = 0x20
# BRTI info: flags, dim, tile mode, swizzle, mips, samples, format, access, width, height, depth, array length,
# layout, layout2, (reserved), image size, alignment, component selection, view dimension, then the name, parent,
# mip pointers, user data, texture, texture view, descriptor slot and user dictionary addresses
INFO = struct.Struct("<2B4H2x2I3i3I20x3IB3x8q")
HEADER = struct.Struct("<8sIH2BI2H2I")
CONTAINER = struct.Struct("<4sI5qI4x")
BLOCK_HEADER = struct.Struct("<4s2I4x")
DICT_NODE = struct.Struct("<I2Hq")
RELOC_SECTION = struct.Struct("<Q2I2i")
RELOC_ENTRY = struct.Struct("<IH2B")


def _round_up(value: int, alignment: int) -> int:
    return -(-value // alignment) * alignment


def _pow2_round_up(value: int) -> int:
    return 1 << (value - 1).bit_length() if value > 0 else 0


def _blocks(size: int) -> int:
    return -(-size // 4)


def _block_height(rows: int) -> int:
    """A texture's block height in GOBs for its height in rows; 0 below 8 rows (one GOB then)."""
    return min(16, _pow2_round_up(rows // GOB_ROWS))


def _mip_layouts(width: int, height: int, mips: int, block_height: int) -> List[Tuple[int, int, int]]:
    """Each mip level's width and height in elements and block height in GOBs, for a compressed texture of the given
    pixel size whose top level has block_height (a power of two, or 0)."""
    log2 = max(0, block_height.bit_length() - 1)
    shift = 0
    layouts = []
    for level in range(mips):
        columns, rows = _blocks(max(1, width >> level)), _blocks(max(1, height >> level))
        if _pow2_round_up(rows) < block_height * GOB_ROWS:
            shift += 1
        layouts.append((columns, rows, 1 << max(0, log2 - shift)))
    return layouts


def surface_size(columns: int, rows: int, element: int, block_height: int) -> int:
    """Bytes of a block-linear surface: whole GOBs across, whole blocks down."""
    return _round_up(columns * element, GOB_WIDTH) * _round_up(rows, block_height * GOB_ROWS)


def _runs(columns: int, rows: int, element: int, block_height: int):
    """(linear offset, block-linear offset, length) of each 16-byte run of a surface's rows."""
    row_bytes = columns * element
    gobs_across = -(-row_bytes // GOB_WIDTH)
    block_bytes = GOB_BYTES * block_height
    block_rows = GOB_ROWS * block_height
    run_offsets = [(x, (x % 64 // 32) * 256 + (x % 32 // 16) * 32 + (x // 64) * block_bytes, min(16, row_bytes - x))
                   for x in range(0, row_bytes, 16)]
    for y in range(rows):
        row = ((y // block_rows) * block_bytes * gobs_across + (y % block_rows // GOB_ROWS) * GOB_BYTES
               + (y % 8 // 2) * 64 + (y % 2) * 16)
        linear = y * row_bytes
        for x, offset, length in run_offsets:
            yield linear + x, row + offset, length


def deswizzle(data: bytes, columns: int, rows: int, element: int, block_height: int) -> bytes:
    """A block-linear surface's elements in row order."""
    out = bytearray(columns * rows * element)
    view = memoryview(data)
    for linear, tiled, length in _runs(columns, rows, element, block_height):
        out[linear:linear + length] = view[tiled:tiled + length]
    return bytes(out)


def swizzle(data: bytes, columns: int, rows: int, element: int, block_height: int) -> bytes:
    """Elements in row order as a block-linear surface (its padding zero)."""
    out = bytearray(surface_size(columns, rows, element, block_height))
    view = memoryview(data)
    for linear, tiled, length in _runs(columns, rows, element, block_height):
        out[tiled:tiled + length] = view[linear:linear + length]
    return bytes(out)


class Texture:
    """One texture: its BRTI info fields (INFO's order), name and the data of all its mip levels."""

    def __init__(self, name: str, info: list, data: bytes, mip_offsets: List[int]):
        self.name = name
        self.info = info
        self.data = data
        self.mip_offsets = mip_offsets

    tile_mode = property(lambda self: self.info[2])
    mips = property(lambda self: self.info[4])
    format = property(lambda self: self.info[6])
    width = property(lambda self: self.info[8])
    height = property(lambda self: self.info[9])
    block_height_log2 = property(lambda self: self.info[12] & 7)
    alignment = property(lambda self: self.info[15])

    def element(self) -> int:
        if self.format >> 8 not in BLOCK_BYTES:
            raise ValueError(f"{self.name}: format {self.format:#x} is not a BCn format")
        return BLOCK_BYTES[self.format >> 8]

    def levels(self) -> List[bytes]:
        """The mip levels' blocks in row order."""
        if self.tile_mode != BLOCK_LINEAR:
            raise ValueError(f"{self.name}: tile mode {self.tile_mode} is not block-linear")
        element = self.element()
        levels = []
        layouts = _mip_layouts(self.width, self.height, self.mips, 1 << self.block_height_log2)
        for offset, (columns, rows, block_height) in zip(self.mip_offsets, layouts):
            if offset + surface_size(columns, rows, element, block_height) > len(self.data):
                raise ValueError(f"{self.name}: its data ends inside a mip level")
            levels.append(deswizzle(self.data[offset:], columns, rows, element, block_height))
        return levels

    def replace(self, format_: int, width: int, height: int, levels: List[bytes]) -> None:
        """Makes this a block-linear texture of the given BCn format and pixel size from its mip levels' blocks (in
        row order); the format's low byte says sRGB (6) or not (1)."""
        element = BLOCK_BYTES[format_ >> 8]
        block_height = _block_height(_blocks(height))
        surfaces, offsets = [], []
        for level, (columns, rows, level_block_height) in zip(
                levels, _mip_layouts(width, height, len(levels), block_height)):
            if len(level) != columns * rows * element:
                raise ValueError(f"{self.name}: a mip level has {len(level)} bytes, not {columns * rows * element}")
            offsets.append(sum(len(s) for s in surfaces))
            surfaces.append(swizzle(level, columns, rows, element, level_block_height))
        log2 = max(0, block_height.bit_length() - 1)
        info = self.info
        info[0] &= 1  # keeps "read the layout", drops sparse binding and residency
        info[1], info[2], info[4], info[6], info[7] = 2, BLOCK_LINEAR, len(levels), format_, ACCESS_TEXTURE
        info[8], info[9], info[11], info[12] = width, height, 1, log2
        info[14], info[15], info[16], info[17] = sum(map(len, surfaces)), TEXTURE_ALIGNMENT, COMPONENTS[format_ >> 8], 1
        self.data = b"".join(surfaces)
        self.mip_offsets = offsets


class Bntx:
    """A BNTX file's textures, with what pack() needs to write it again (its name, string table and name
    dictionary)."""

    def __init__(self, data: bytes):
        magic, _, bom, _, _, name_at, _, strings_at, _, _ = HEADER.unpack_from(data, 0)
        if magic != b"BNTX\0\0\0\0" or bom != 0xfeff:
            raise ValueError("not a little-endian BNTX file")
        self.target, count, info_pointers, _, dict_at, _, _, _ = CONTAINER.unpack_from(data, 0x20)
        if self.target != b"NX  ":
            raise ValueError(f"BNTX for {self.target!r}, not the Switch")
        self.strings_magic = data[strings_at:strings_at + 4]
        # The string table: its count, 4 bytes, then each string as a 16-bit length, its bytes and a 0, at even
        # positions
        self.strings: List[bytes] = []
        index_at = {}
        position = strings_at + 16 + 8
        for i in range(struct.unpack_from("<I", data, strings_at + 16)[0]):
            length = struct.unpack_from("<H", data, position)[0]
            index_at.setdefault(position, i)
            self.strings.append(bytes(data[position + 2:position + 2 + length]))
            position = _round_up(position + length + 3, 2)
        self.name_index = index_at[name_at - 2]
        # The texture name dictionary (a Patricia trie): its nodes are kept, their names by string index
        self.dict_magic, dict_count = struct.unpack_from("<4sI", data, dict_at)
        self.dict_nodes = []
        for i in range(dict_count + 1):
            reference, left, right, name = DICT_NODE.unpack_from(data, dict_at + 8 + 16 * i)
            self.dict_nodes.append((reference, left, right, index_at[name] if i else -1))
        self.textures: List[Texture] = []
        self.name_indices: List[int] = []
        for i in range(count):
            block = struct.unpack_from("<q", data, info_pointers + 8 * i)[0]
            if data[block:block + 4] != b"BRTI":
                raise ValueError(f"texture {i} has no BRTI block")
            info = list(INFO.unpack_from(data, block + 16))
            mips, size, name_addr, mip_pointers = info[4], info[14], info[18], info[20]
            pointers = struct.unpack_from(f"<{mips}q", data, mip_pointers)
            self.name_indices.append(index_at[name_addr])
            self.textures.append(Texture(self.strings[index_at[name_addr]].decode("utf-8"), info,
                                         bytes(data[pointers[0]:pointers[0] + size]),
                                         [p - pointers[0] for p in pointers]))

    def texture(self, name: str) -> Texture:
        texture = next((t for t in self.textures if t.name == name), None)
        if texture is None:
            raise ValueError(f"the BNTX has no texture {name}")
        return texture

    def pack(self) -> bytes:
        """The BNTX file of these textures."""
        count = len(self.textures)
        alignment = 1 << ALIGNMENT_SHIFT
        relocations: List[Tuple[int, int, int, int]] = []  # (position, structs, pointers per struct, padding)

        def pointer_runs(position: int, total: int, struct_count: bool, step: int) -> None:
            """Relocation entries for total pointers from position, step bytes apart, at most 255 per entry."""
            while total > 0:
                run = min(total, 0xff)
                relocations.append((position, run, 1, 1) if struct_count else (position, 1, run, 0))
                position += run * step
                total -= run

        relocations += [(0x28, 2, 1, 1), (0x40, 1, 1, 0)]  # the container's info, data and dictionary pointers
        pointer_runs(INFO_POINTERS, count, False, 8)

        strings_at = INFO_POINTERS + 8 * count + 16
        table = bytearray(struct.pack("<I4x", len(self.strings)))
        string_at = []
        for string in self.strings:
            string_at.append(strings_at + len(table))
            table += struct.pack("<H", len(string)) + string + b"\0"
            table += bytes(_round_up(strings_at + len(table), 2) - strings_at - len(table))
        empty_at = strings_at + 4  # the dictionary's root names the empty string

        dict_at = _round_up(strings_at + len(table), 8)
        dictionary = bytearray(dict_at - strings_at - len(table))
        dictionary += struct.pack("<4sI", self.dict_magic, len(self.dict_nodes) - 1)
        for reference, left, right, index in self.dict_nodes:
            dictionary += DICT_NODE.pack(reference, left, right, string_at[index] if index >= 0 else empty_at)
        pointer_runs(dict_at + 16, len(self.dict_nodes), True, 16)
        strings_size = len(table) + len(dictionary) + 16

        infos_at = strings_at - 16 + strings_size
        data_at = infos_at + 16 + sum(INFO_BLOCK + 8 * t.mips for t in self.textures)
        infos_size = data_at - 16
        data_padding = _round_up(data_at, alignment) - data_at
        data_at += data_padding

        info_blocks = bytearray()
        data = bytearray()
        for i, texture in enumerate(self.textures):
            at = infos_at + len(info_blocks) + 16
            relocations += [(at + 0x50, 1, 3, 0), (at + 0x68, 1, 1, 0), (at + 0x70, 1, 2, 0), (at + 0x88, 1, 1, 0)]
            size = INFO_BLOCK + 8 * texture.mips + (data_padding if i == count - 1 else 0)
            info = list(texture.info)
            info[12] = texture.block_height_log2
            info[18:26] = [string_at[self.name_indices[i]], 0x20, at + 0x290, 0, at + 0x90, at + 0x190, 0, 0]
            info_blocks += BLOCK_HEADER.pack(b"BRTI", size, size) + INFO.pack(*info) + bytes(0x200)
            data += bytes(_round_up(data_at + len(data), texture.alignment) - data_at - len(data))
            info_blocks += struct.pack(f"<{texture.mips}q", *(data_at + len(data) + o for o in texture.mip_offsets))
            data += texture.data
        reloc_at = _round_up(data_at + len(data), alignment)
        data += bytes(reloc_at - data_at - len(data))
        data_size = len(data) + 16

        section0 = len(relocations)
        relocations.append((0x30, 1, 1, 0))  # the container's data block pointer
        for texture, at in zip(self.textures, self._info_positions(infos_at)):
            pointer_runs(at + 0x290, texture.mips, False, 8)
        reloc = bytearray(BLOCK_HEADER.pack(b"_RLT", reloc_at, 2))
        reloc += RELOC_SECTION.pack(0, 0, infos_size, 0, section0)
        reloc += RELOC_SECTION.pack(0, data_at - 16, data_size, section0, len(relocations) - section0)
        for entry in relocations:
            reloc += RELOC_ENTRY.pack(*entry)

        body = bytearray(CONTAINER.pack(self.target, count, INFO_POINTERS, data_at - 16, dict_at, 0x58, 0, 0))
        body += bytes(0x140)
        body += struct.pack(f"<{count}q", *(at - 16 for at in self._info_positions(infos_at)))
        body += BLOCK_HEADER.pack(self.strings_magic, strings_size, strings_size) + table + dictionary
        body += info_blocks + bytes(data_padding) + BLOCK_HEADER.pack(b"BRTD", 0, data_size) + data + reloc
        header = HEADER.pack(b"BNTX\0\0\0\0", 0x40100, 0xfeff, ALIGNMENT_SHIFT, 0x40,
                             string_at[self.name_index] + 2, 0, strings_at - 16, reloc_at, len(body) + 32)
        return header + bytes(body)

    def _info_positions(self, infos_at: int) -> List[int]:
        """Where each texture's info starts (after its BRTI header)."""
        positions = []
        at = infos_at + 16
        for texture in self.textures:
            positions.append(at)
            at += INFO_BLOCK + 8 * texture.mips
        return positions


def find_bntx(bfres: bytes) -> Tuple[int, int]:
    """Where a bfres model's embedded BNTX starts and how many bytes its section has (from the relocation table)."""
    if bfres[:8] != b"FRES    ":
        raise ValueError("not a bfres file")
    reloc_at = struct.unpack_from("<I", bfres, 0x18)[0]
    magic, _, sections = BLOCK_HEADER.unpack_from(bfres, reloc_at)
    if magic != b"_RLT":
        raise ValueError("the bfres has no relocation table")
    for i in range(sections):
        _, at, size, _, _ = RELOC_SECTION.unpack_from(bfres, reloc_at + 16 + RELOC_SECTION.size * i)
        if bfres[at:at + 4] == b"BNTX":
            return at, size
    raise ValueError("the bfres has no BNTX")


def replace_bntx(bfres: bytes, packed: bytes, at: int, size: int) -> bytearray:
    """The bfres with its BNTX section overwritten from its start by packed (which must fit its section)."""
    if len(packed) > size:
        raise ValueError("the textures do not fit the model file")
    out = bytearray(bfres)
    out[at:at + len(packed)] = packed
    return out


def open_bfres(bfres: bytes) -> Tuple[Bntx, int, int]:
    at, size = find_bntx(bfres)
    return Bntx(bfres[at:at + size]), at, size

