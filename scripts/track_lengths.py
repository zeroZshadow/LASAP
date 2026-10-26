"""Writes how long each Jukebox track plays through into las-ap-exl/source/program/music/music_tracks.hpp.

A BGM label leads through the sound archive (region_common/audio/Richard.bfsar: its string tree, sound infos and
file infos) to its stream file (region_common/audio/stream/*.bfstm), whose stream info holds the sample rate, the
loop flag, the loop start and the end. A track plays through once at its end; a looping track then repeats its loop
until at least MIN_SECONDS have passed, so a short loop is heard a few times before the Jukebox moves on. The Ballad
of the Wind Fish's length is its looping copy's, which the patcher makes (patcher/ap_ballad.py).

Run with any Python 3: python track_lengths.py [--romfs <RomFS folder>] [--check]
--check only reports entries whose length differs from the RomFS's.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

MIN_SECONDS = 60
ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "las-ap-exl" / "source" / "program" / "music" / "music_tracks.hpp"
sys.path.insert(0, str(ROOT / "patcher"))
import ap_ballad  # noqa: E402
from local_paths import local_path  # noqa: E402
# {"BGM_...", "name", "info"} or {"BGM_...", "name", "info", ms}
ENTRY = re.compile(r'(\{\s*"(BGM_\w+)"\s*,\s*"(?:[^"\\]|\\.)*"\s*,\s*"(?:[^"\\]|\\.)*")(?:\s*,\s*\d+)?(\s*\})')


def sections(data: bytes) -> dict:
    count = struct.unpack_from("<H", data, 0x10)[0]
    found = {}
    for i in range(count):
        kind, offset, _ = struct.unpack_from("<HxxiI", data, 0x14 + i * 12)
        found[kind] = offset
    return found


def stream_paths(romfs: Path) -> dict:
    """BGM label -> stream file path, relative to region_common/audio."""
    data = (romfs / "region_common" / "audio" / "Richard.bfsar").read_bytes()
    assert data[:4] == b"FSAR"

    def ref(base, at):
        kind, offset = struct.unpack_from("<Hxxi", data, at)
        return kind, base + offset

    def table(base):
        count = struct.unpack_from("<I", data, base)[0]
        return [ref(base, base + 4 + 8 * i) for i in range(count)]

    def cstr(at):
        return data[at:data.index(b"\0", at)].decode()

    secs = sections(data)
    strg = secs[0x2000] + 8
    _, strings = ref(strg, strg)
    _, tree = ref(strg, strg + 8)
    names = []
    for i in range(struct.unpack_from("<I", data, strings)[0]):
        _, offset, _ = struct.unpack_from("<HxxiI", data, strings + 4 + 12 * i)
        names.append(cstr(strings + offset))
    item_names = {}
    _, nodes = struct.unpack_from("<II", data, tree)
    for i in range(nodes):
        flags, _, _, _, string, item = struct.unpack_from("<HHIIII", data, tree + 8 + 20 * i)
        if flags & 1 and string < len(names):
            item_names[item] = names[string]
    info = secs[0x2001] + 8
    refs = dict(ref(info, info + 8 * k) for k in range(8))
    sounds, files = table(refs[0x2100]), table(refs[0x2106])
    paths = {}
    for index, (_, sound) in enumerate(sounds):
        name = item_names.get((1 << 24) | index, "")
        if not name.startswith("BGM_"):
            continue
        _, file_info = files[struct.unpack_from("<I", data, sound)[0]]
        kind, location = ref(file_info, file_info)
        if kind == 0x220D:  # an external file
            paths[name] = cstr(location)
    return paths


def play_ms(data: bytes) -> int:
    info = sections(data)[0x4000] + 8
    _, offset = struct.unpack_from("<Hxxi", data, info)
    _, loop, _, _, rate, start, end = struct.unpack_from("<BBBBIII", data, info + offset)
    samples = end
    if loop and end > start:
        while samples < MIN_SECONDS * rate:
            samples += end - start
    return round(samples * 1000 / rate)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    romfs = Path(args.romfs or local_path("romfs", "LASAP_ROMFS"))
    paths = stream_paths(romfs)
    text = HEADER.read_bytes().decode("utf-8")
    missing, changed = [], []

    def entry(match):
        label = match.group(2)
        if label not in paths:
            missing.append(label)
            return match.group(0)
        data = (romfs / "region_common" / "audio" / paths[label]).read_bytes()
        if label == ap_ballad.LABEL:  # the Jukebox plays the patcher's looping copy
            data = ap_ballad.loop_stream(data, ap_ballad.LOOP_END)
        ms = play_ms(data)
        new = f"{match.group(1)}, {ms}{match.group(3)}"
        if new != match.group(0):
            changed.append(label)
        return new

    result = ENTRY.sub(entry, text)
    if missing:
        sys.exit(f"no stream for {', '.join(missing)}")
    if args.check:
        print("lengths differ: " + ", ".join(changed) if changed else "lengths match")
        sys.exit(1 if changed else 0)
    HEADER.write_bytes(result.encode("utf-8"))
    print(f"{len(changed)} lengths written")


if __name__ == "__main__":
    main()
