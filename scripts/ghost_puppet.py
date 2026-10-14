"""Sets up the test build's ghost puppet (DBG puppet, ap_puppet.cpp) in the sandbox mod, or takes it out again.

install: writes romfs/region_common/actor/PlayerGhost.bfres, Link's model for a ghost: the albedo textures of the
materials a ghost shows as BC3 with a constant alpha (their colour blocks kept) in render pass 1, so the module's
alpha-to-coverage dithers them in screen space; the textures a ghost never shows shrunk to 4x4 to make room. Then
adds the row APGhost to the deployed Npc.gsheet, NpcGhost's row drawing that model (model "link", Link's
animations, not talkable), keeping the sheet as Npc.gsheet.before-puppet. The module spawns ghosts as NpcGhost
actors that read the APGhost row.
uninstall: removes the model and puts the kept Npc.gsheet back.

Run with .venv-lasr (patcher/requirements-assets.txt):
  python scripts/ghost_puppet.py install|uninstall [--romfs D:/Games/Switch/LASRomFS] [--contents <mod contents>]
"""
import argparse
import io
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "patcher"))
from RandomizerCore.Tools.bntx_editor import bfres, bntx_editor  # noqa: E402
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct, readSheet, writeSheet  # noqa: E402

CONTENTS = Path("D:/DEV/LASAP-sandbox/ryujinx/sdcard/atmosphere/contents/01006BB00C6F0000")
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


def ghost_model(source: Path) -> bytes:
    model = bfres.File()
    model.readFromFile(str(source))
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
                raise SystemExit(f"could not convert {name}")
        elif not alpha and not shown and texture.format_ in FORMAT_FOURCC:
            fourcc = FORMAT_FOURCC[texture.format_]
            block = 16 if fourcc in (b"DXT5", b"BC5U", b"BC5S") else 8
            new = textures.replace(texture, texture.tileMode, (texture.format_ & 0xff) == 6, False, False, False,
                                   dds(4, 4, fourcc, [bytes(block)]))
        if new:
            textures.textures[index] = new
    packed = editor.save()
    if len(packed) > size:
        raise SystemExit("the textures do not fit the model file")
    raw = bytearray(model.rawData)
    raw[start:start + len(packed)] = packed
    if set_render_pass(raw, SHOWN_MATERIALS, 1) == 0:
        raise SystemExit("no render pass option found")
    return bytes(raw)


def add_ghost_row(src: Path, dst: Path) -> None:
    sheet = readSheet(str(src))
    rows = [parseStruct(value) for value in sheet["values"]]
    sheet["values"] = [value for value, row in zip(sheet["values"], rows) if row["symbol"] != "APGhost"]
    for row in rows:
        if row["symbol"] != "NpcGhost":
            continue
        row["symbol"] = "APGhost"
        graphics = row["graphics"]
        graphics["path"] = "PlayerGhost.bfres"
        graphics["model"] = "link"
        graphics["animations"] = {"idle": "wait", "talk": "talk", "walk": "walk", "run": "run"}
        graphics["animSettings"] = [{"name": n, "blendTime": 4, "lipsync": 0, "cull": 2}
                                    for n in ("wait", "talk", "walk", "run")]
        graphics["facial"] = ""
        row["eventTriggers"] = []
        row["doAction"]["type"] = 0
        row["layoutConditions"] = []
        sheet["values"].append(dictToStruct(row))
        writeSheet(str(dst), sheet)
        return
    raise SystemExit("no NpcGhost row")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("install", "uninstall"))
    parser.add_argument("--romfs", type=Path, default=Path("D:/Games/Switch/LASRomFS"))
    parser.add_argument("--contents", type=Path, default=CONTENTS)
    args = parser.parse_args()
    model_path = args.contents / "romfs" / "region_common" / "actor" / "PlayerGhost.bfres"
    sheet_path = args.contents / "romfs" / "region_common" / "datasheets" / "Npc.gsheet"
    kept = sheet_path.with_name("Npc.gsheet.before-puppet")
    if args.action == "install":
        model_path.parent.mkdir(parents=True, exist_ok=True)
        model_path.write_bytes(ghost_model(args.romfs / "region_common" / "actor" / "Playerlink.bfres"))
        if not kept.exists():  # empty: the mod had no Npc.gsheet of its own
            kept.parent.mkdir(parents=True, exist_ok=True)
            kept.write_bytes(sheet_path.read_bytes() if sheet_path.exists() else b"")
        source = kept if kept.stat().st_size else args.romfs / "region_common" / "datasheets" / "Npc.gsheet"
        add_ghost_row(source, sheet_path)
        print("installed", model_path.name, "and the APGhost row")
    else:
        model_path.unlink(missing_ok=True)
        if kept.exists():
            if kept.stat().st_size:
                sheet_path.write_bytes(kept.read_bytes())
            else:
                sheet_path.unlink(missing_ok=True)
            kept.unlink()
        print("uninstalled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
