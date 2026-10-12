"""Archipelago look for the autosave popup ("Now saving.", Ld_ThrobberSave in StartUp.arc), which the module also
uses for its notices (toasts): a wider text pane with room for longer text, and the Archipelago logo in place of
the egg. The egg texture (Loading_00) is shared with the loading screen's throbber, which shows the logo too."""
import io
import math
import struct
from pathlib import Path

import oead

ARCHIVE = Path("region_common", "ui", "StartUp.arc")
LAYOUT = "blyt/Ld_ThrobberSave.bflyt"
TEXTURES = "timg/__Combined.bntx"
ICON = "Loading_00^f"
TEXT_PANE = b"T_Info_00"
# The pane is anchored at its right edge, next to the icon; a wider pane grows to the left.
TEXT_WIDTH = 640.0
TEXT_BUFFER_BYTES = 512  # UTF-16, with the terminator
# Six circles in a ring, as in the Archipelago logo and the 3D model (ap_model.py); in texture pixels.
RING_RADIUS = 13.5
CIRCLE_RADIUS = 8.5
OUTLINE = 1.2  # transparent edge around each circle, so overlapping circles stay apart
SUPERSAMPLE = 4

PANE_HEADER = 8      # section magic and size
PANE_NAME = 4        # in the pane body: flags, origin, alpha, scale flags, then the name (24 bytes)
PANE_SIZE = 0x44     # width, height
TEXT_FIELDS = 0x4c   # txt1 fields after the pane body: text buffer bytes (u16), ...


def _patch_layout(data: bytes) -> bytes:
    out = bytearray(data)
    off = 0x14
    while off < len(out):
        magic = bytes(out[off:off + 4])
        size = struct.unpack_from("<I", out, off + 4)[0]
        body = off + PANE_HEADER
        if magic == b"txt1" and bytes(out[body + PANE_NAME:body + PANE_NAME + 24]).split(b"\0")[0] == TEXT_PANE:
            struct.pack_into("<f", out, body + PANE_SIZE, TEXT_WIDTH)
            struct.pack_into("<H", out, body + TEXT_FIELDS, TEXT_BUFFER_BYTES)
            return bytes(out)
        off += size
    raise ValueError(f"{LAYOUT} has no text pane {TEXT_PANE.decode()}")


def _logo_alpha(size: int) -> bytes:
    """Alpha of the white logo, size x size, antialiased by supersampling."""
    n = size * SUPERSAMPLE
    center = n / 2
    circles = [(center + RING_RADIUS * SUPERSAMPLE * math.cos(math.radians(90 + 60 * i)),
                center - RING_RADIUS * SUPERSAMPLE * math.sin(math.radians(90 + 60 * i))) for i in range(6)]
    radius = CIRCLE_RADIUS * SUPERSAMPLE
    outline = (CIRCLE_RADIUS + OUTLINE) * SUPERSAMPLE
    alpha = []
    for y in range(size):
        for x in range(size):
            hits = 0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    px, py = x * SUPERSAMPLE + sx + 0.5, y * SUPERSAMPLE + sy + 0.5
                    inside = False
                    # later circles cover earlier ones; each one's outline cuts the ones below it
                    for cx, cy in circles:
                        d = math.hypot(px - cx, py - cy)
                        if d <= radius:
                            inside = True
                        elif d <= outline:
                            inside = False
                    hits += inside
            alpha.append(round(255 * hits / SUPERSAMPLE ** 2))
    return bytes(alpha)


def _logo_dds(size: int) -> io.BytesIO:
    """Luminance + alpha DDS (A8L8): white everywhere, the logo in alpha."""
    pixels = b"".join(bytes((255, a)) for a in _logo_alpha(size))
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<7I", header, 4, 124, 0x100f, size, size, size * 2, 0, 1)
    struct.pack_into("<2I", header, 76, 32, 0x20001)          # pixel format: luminance + alpha
    struct.pack_into("<5I", header, 88, 16, 0xff, 0xff, 0xff, 0xff00)
    struct.pack_into("<I", header, 108, 0x1000)
    return io.BytesIO(bytes(header) + pixels)


def _patch_textures(data: bytes) -> bytes:
    from RandomizerCore.Tools.bntx_editor import bntx_editor
    editor = bntx_editor.BNTXEditor()
    editor.openFile(data)
    for index, texture in enumerate(editor.bntx.textures):
        if texture.name == ICON:
            new = editor.bntx.replace(texture, texture.tileMode, False, False, False, False,
                                      _logo_dds(texture.width))
            if not new:
                raise ValueError(f"cannot replace {ICON}")
            editor.bntx.textures[index] = new
            return bytes(editor.save())
    raise ValueError(f"{TEXTURES} has no texture {ICON}")


def build(romfs: Path) -> bytes:
    """StartUp.arc with the popup changes, from the RomFS dump."""
    sarc = oead.Sarc((romfs / ARCHIVE).read_bytes())
    writer = oead.SarcWriter.from_sarc(sarc)
    # the alignments of the original archive
    writer.set_min_alignment(16)
    writer.add_alignment_requirement("bnsh", 0x1000)
    writer.add_alignment_requirement("bntx", 0x1000)
    writer.files[LAYOUT] = _patch_layout(bytes(sarc.get_file(LAYOUT).data))
    writer.files[TEXTURES] = _patch_textures(bytes(sarc.get_file(TEXTURES).data))
    return bytes(writer.write()[1])
