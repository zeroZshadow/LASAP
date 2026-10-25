"""The Jukebox's Ballad of the Wind Fish loops (DESIGN.md, Music).

The Wind Fish's Egg plays BGM_ENSEMBLE_INST (a track per instrument) once: 20 bars at 127.8 BPM, then the last notes'
decay. The patcher writes a copy that loops, `BGM_ENSEMBLE_LOOP.bfstm` beside it, which the module plays for the
Jukebox alone; the egg keeps the original. The copy ends on the downbeat of bar 20, where the decay begins, and loops
from its start, the downbeat of bar 0: the accompaniment's two-bar pattern goes on across the seam. Loop start 0 is on a
block boundary, as the game's looping streams have theirs, and its decoder state is the stream's own start state."""
import struct
from pathlib import Path

STREAM_DIR = Path("region_common") / "audio" / "stream"
LABEL = "BGM_ENSEMBLE_INST"
SOURCE = f"{LABEL}.bfstm"
# The module opens this file in the source's place (music.cpp): the same length, so the path swap stays in place.
LOOPED = "BGM_ENSEMBLE_LOOP.bfstm"
# The downbeat of bar 20: 20 bars of 90138 samples (127.8 BPM at 48 kHz, from the tracks' note onsets).
LOOP_END = 1802760

INFO, SEEK, DATA = 0x4000, 0x4001, 0x4002
DSP_ADPCM = 2
FRAME_BYTES, FRAME_SAMPLES = 8, 14


def align(value: int, to: int = 0x20) -> int:
    return (value + to - 1) // to * to


def loop_stream(data: bytes, end: int) -> bytes:
    """A FSTM 0.6 DSP-ADPCM stream cut at sample `end`, looping from sample 0."""
    assert data[:4] == b"FSTM" and struct.unpack_from("<I", data, 8)[0] >> 16 == 6, "not a FSTM 0.6 stream"
    sections = {}
    for i in range(struct.unpack_from("<H", data, 0x10)[0]):
        kind, offset, size = struct.unpack_from("<HxxiI", data, 0x14 + 12 * i)
        sections[kind] = (offset, size)
    info = sections[INFO][0] + 8
    stream = info + struct.unpack_from("<Hxxi", data, info)[1]
    codec, _, channels = struct.unpack_from("<BBB", data, stream)
    samples, _, block_size, block_samples = struct.unpack_from("<IIII", data, stream + 0xc)
    seek_size = struct.unpack_from("<I", data, stream + 0x28)[0]
    data_offset = struct.unpack_from("<Hxxi", data, stream + 0x30)[1]
    assert codec == DSP_ADPCM and 0 < end < samples
    new_blocks = (end + block_samples - 1) // block_samples
    last_samples = end - (new_blocks - 1) * block_samples
    last_size = (last_samples + FRAME_SAMPLES - 1) // FRAME_SAMPLES * FRAME_BYTES
    last_padded = align(last_size)

    out = bytearray(data[:sections[SEEK][0]])
    out[stream + 1] = 1  # loop flag
    struct.pack_into("<II", out, stream + 8, 0, end)  # loop start, sample count
    struct.pack_into("<IIIIII", out, stream + 0x10, new_blocks, block_size, block_samples, last_size, last_samples,
                     last_padded)
    struct.pack_into("<II", out, stream + 0x44, 0, end)  # the original loop start and end
    # each channel's loop context: its start context (predictor/scale and history), as the loop starts at sample 0
    table = info + struct.unpack_from("<Hxxi", data, info + 16)[1]
    for c in range(channels):
        channel = table + struct.unpack_from("<Hxxi", data, table + 4 + 8 * c)[1]
        adpcm = channel + struct.unpack_from("<Hxxi", data, channel)[1]
        out[adpcm + 0x26:adpcm + 0x2c] = data[adpcm + 0x20:adpcm + 0x26]

    seek_offset = sections[SEEK][0]
    seek_body = data[seek_offset + 8:seek_offset + 8 + new_blocks * channels * seek_size]
    seek = bytearray(b"SEEK" + struct.pack("<I", align(8 + len(seek_body))) + seek_body)
    seek.extend(bytes(align(len(seek)) - len(seek)))

    source = sections[DATA][0] + 8 + data_offset
    body = bytearray()
    for b in range(new_blocks):
        size = block_size if b < new_blocks - 1 else last_padded
        for c in range(channels):
            start = source + (b * channels + c) * block_size
            body += data[start:start + size]
    data_header = bytearray(b"DATA" + struct.pack("<I", 8 + data_offset + len(body)))
    data_header.extend(bytes(data_offset))

    new_data = len(out) + len(seek)
    out += seek + data_header + body
    for i in range(struct.unpack_from("<H", data, 0x10)[0]):
        kind = struct.unpack_from("<H", data, 0x14 + 12 * i)[0]
        if kind == SEEK:
            struct.pack_into("<iI", out, 0x14 + 12 * i + 4, seek_offset, len(seek))
        elif kind == DATA:
            struct.pack_into("<iI", out, 0x14 + 12 * i + 4, new_data, len(data_header) + len(body))
    struct.pack_into("<I", out, 0xc, len(out))
    return bytes(out)


def add(romfs: Path, romfs_dir: Path) -> None:
    """Writes the Jukebox's looping copy of the ensemble into the mod's RomFS."""
    looped = loop_stream((romfs / STREAM_DIR / SOURCE).read_bytes(), LOOP_END)
    path = romfs_dir / STREAM_DIR / LOOPED
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(looped)
