"""The seashell chest: LASR's chest textures ("Texture + Size") with a blue one for chests holding a seashell.

ObjTreasureBoxSeashell.bfres is the plain chest (ObjTreasureBox.bfres) with its albedo recolored: the gold trim
turns blue and the red wood dark blue, the purple keeps its color. The asset build (ap_assets.py) makes it from the
RomFS with Pillow and quicktex; the patcher only applies its patch.
"""
import io

KIND = "Seashell"
CHEST_TYPE = "seashell"  # chest-type of the seashell in LASR's item definitions
FILE_NAME = f"ObjTreasureBox{KIND}.bfres"
ALBEDO = "MI_dungeonTreasureBox_01_alb"


def _hue(deg: float) -> int:
    """Pillow's HSV hue, 0-255 for a full turn."""
    return round(deg * 256 / 360) % 256


def _blue(h: int) -> int:
    deg = h * 360 / 256
    if 15 <= deg < 70:  # gold trim
        return _hue(210 + (deg - 40) * 0.4)
    if deg < 15 or deg >= 320:  # red wood
        return _hue(228 + ((deg + 40) % 360 - 40) * 0.4)
    return h


def recolor(image):
    """The plain chest's albedo (a Pillow image) in blue."""
    from PIL import Image

    h, s, v = image.convert("RGB").convert("HSV").split()
    return Image.merge("HSV", (h.point(_blue), s, v)).convert("RGB")


def build(source: str) -> bytes:
    """ObjTreasureBoxSeashell.bfres from the path of ObjTreasureBox.bfres (needs the native packages)."""
    import quicktex.dds as quicktex_dds
    import quicktex.s3tc.bc1 as bc1
    from PIL import Image
    from RandomizerCore.Tools.bntx_editor import bfres, bntx_editor
    from RandomizerCore.Tools.bntx_tools import save as save_dds

    chest = bfres.File()
    chest.readFromFile(source)
    start, size = chest.bfresTexture.pos, chest.bfresTexture.size
    editor = bntx_editor.BNTXEditor()
    editor.openFile(bytes(chest.rawData[start:start + size]))
    textures = editor.bntx.textures
    index = next(i for i, t in enumerate(textures) if t.name == ALBEDO)
    texture = textures[index]
    blue = recolor(Image.open(io.BytesIO(editor.exportTexByIndex(index))))
    dds = io.BytesIO()
    save_dds(quicktex_dds.encode(blue, bc1.BC1Encoder(18), "DXT1", texture.numMips), dds)
    dds.seek(0)
    new = editor.bntx.replace(texture, texture.tileMode, (texture.format_ & 0xff) == 6, False, False, True, dds)
    if not new:
        raise ValueError(f"cannot replace {ALBEDO}")
    textures[index] = new
    packed = editor.save()
    if len(packed) > size:
        raise ValueError("the recolored textures do not fit the chest file")
    raw = bytearray(chest.rawData)
    raw[start:start + len(packed)] = packed
    return bytes(raw)
