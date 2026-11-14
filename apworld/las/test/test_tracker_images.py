"""The tracker's map images (patcher ap_tracker_map.py): its ASTC decoder (ap_astc.py), BC3 icons (ap_bntx.py) and how
it draws and writes the images."""
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

PATCHER = Path(__file__).resolve().parents[3] / "patcher"


def texels(rgba: bytes):
    return [rgba[i:i + 4] for i in range(0, len(rgba), 4)]


class TestTrackerImages(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        saved = list(sys.path)
        sys.path.insert(0, str(PATCHER))
        try:
            import ap_astc
            import ap_bntx
            import ap_tracker_map
        finally:
            sys.path[:] = saved
        cls.astc, cls.bntx, cls.images = ap_astc, ap_bntx, ap_tracker_map

    def test_astc_blocks(self) -> None:
        """Blocks of the game's textures, one per kind (block size, partitions, planes, ranges, endpoint mode, void
        extent), decode within 1 of a reference decoder (texture2ddecoder)."""
        for block, bw, bh, want in BLOCKS:
            with self.subTest(block=block):
                got = self.astc.decode_block(bytes.fromhex(block), bw, bh)
                self.assertLessEqual(max(abs(a - b) for a, b in zip(got, bytes.fromhex(want))), 1)

    def test_astc_step(self) -> None:
        """A step decodes the texels at its multiples."""
        for block, bw, bh, _ in BLOCKS:
            full = texels(self.astc.decode_block(bytes.fromhex(block), bw, bh))
            for step in (s for s in (2, 4, 5) if bw % s == 0 and bh % s == 0):
                with self.subTest(block=block, step=step):
                    want = [full[y * bw + x] for y in range(0, bh, step) for x in range(0, bw, step)]
                    self.assertEqual(texels(self.astc.decode_block(bytes.fromhex(block), bw, bh, step)), want)

    def test_astc_texture(self) -> None:
        """A texture's blocks go in row order, its size rounded up to whole blocks."""
        a, b = [bytes.fromhex(block) for block, bw, bh, _ in BLOCKS if (bw, bh) == (4, 4)][1:3]
        rgba, w, h = self.astc.decode(a + b, 6, 3, 4, 4)
        self.assertEqual((w, h), (8, 4))
        rows = [texels(rgba)[y * 8:(y + 1) * 8] for y in range(4)]
        ta, tb = texels(self.astc.decode_block(a, 4, 4)), texels(self.astc.decode_block(b, 4, 4))
        self.assertEqual(rows, [ta[y * 4:(y + 1) * 4] + tb[y * 4:(y + 1) * 4] for y in range(4)])

    def test_bc3(self) -> None:
        """BC3: 8 alpha levels between two endpoints, 4 colours from two RGB565 ones."""
        alpha_indices = sum(i << (3 * p) for p, i in enumerate([0, 1, 2, 3, 4, 5, 6, 7] * 2))
        colour_indices = sum((p % 4) << (2 * p) for p in range(16))
        block = (bytes([255, 0]) + alpha_indices.to_bytes(6, "little") + struct.pack("<2HI", 0xf800, 0x001f,
                                                                                   colour_indices))
        got = texels(self.bntx.decode_bc3(block, 4, 4))
        colours = [(255, 0, 0), (0, 0, 255), (170, 0, 85), (85, 0, 170)]
        alphas = [255, 0, 218, 182, 145, 109, 72, 36]
        self.assertEqual(got, [bytes((*colours[p % 4], alphas[p % 8])) for p in range(16)])

    def test_with_alpha(self) -> None:
        """A piece takes its underlay's alpha; outside the underlay it is clear."""
        piece = bytes([9, 8, 7, 255]) * 6  # 3x2
        mask = bytes([0, 0, 0, 40, 0, 0, 0, 200])  # 2x1
        got = texels(self.images.with_alpha(piece, 3, 2, mask, 2, 1))
        self.assertEqual([t[3] for t in got], [40, 200, 0, 0, 0, 0])
        self.assertEqual({t[:3] for t in got}, {bytes([9, 8, 7])})

    def test_trim(self) -> None:
        """An icon is cut to the box of what it draws; faint texels around it go too."""
        image = bytearray(4 * 4 * 4)  # 4x4, drawn at (1, 1)-(2, 2), faint at (3, 0)
        for x, y, a in ((1, 1, 255), (2, 2, 255), (3, 0, self.images.TRIM_ALPHA)):
            image[(y * 4 + x) * 4:(y * 4 + x + 1) * 4] = bytes([x, y, 0, a])
        rgba, w, h = self.images.trim(bytes(image), 4, 4)
        self.assertEqual((w, h), (2, 2))
        self.assertEqual(texels(rgba), [bytes([1, 1, 0, 255]), bytes(4), bytes(4), bytes([2, 2, 0, 255])])

    def test_composite(self) -> None:
        """Opaque texels replace the canvas, clear ones leave it, partly clear ones blend (straight alpha); an image
        reaching past the canvas is cut."""
        canvas = bytearray(bytes([100, 100, 100, 255]) * 4)  # 2x2
        image = bytes([200, 0, 0, 255, 0, 200, 0, 0, 9, 9, 9, 255,  # 3x2: its third column is past the canvas
                       0, 0, 200, 51, 1, 1, 1, 255, 9, 9, 9, 255])
        self.images.composite(canvas, 2, 2, image, 3, 2, 0, 0)
        self.assertEqual(texels(bytes(canvas)), [bytes([200, 0, 0, 255]), bytes([100, 100, 100, 255]),
                                                 bytes([80, 80, 120, 255]), bytes([1, 1, 1, 255])])
        clear = bytearray(4)
        self.images.composite(clear, 1, 1, bytes([0, 0, 200, 51]), 1, 1, 0, 0)
        self.assertEqual(bytes(clear), bytes([0, 0, 200, 51]))

    def test_png(self) -> None:
        """An RGBA PNG whose one IDAT holds the rows unfiltered."""
        rgba = bytes(range(24))  # 3x2
        data = self.images.png(rgba, 3, 2)
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        chunks, pos = {}, 8
        while pos < len(data):
            size, kind = struct.unpack_from(">I4s", data, pos)
            body = data[pos + 8:pos + 8 + size]
            self.assertEqual(struct.unpack_from(">I", data, pos + 8 + size)[0], zlib.crc32(kind + body))
            chunks[kind] = body
            pos += 12 + size
        self.assertEqual(struct.unpack(">2I5B", chunks[b"IHDR"]), (3, 2, 8, 6, 0, 0, 0))
        self.assertEqual(zlib.decompress(chunks[b"IDAT"]), b"\0" + rgba[:12] + b"\0" + rgba[12:])
        self.assertIn(b"IEND", chunks)

    def test_level_cells(self) -> None:
        """The cells a level has a room of that name in, by its rooms' files: not a room in two variants (L, U)."""
        with tempfile.TemporaryDirectory() as romfs:
            level = Path(romfs, self.images.LEVEL, "Lv04AnglersTunnel")
            level.mkdir(parents=True)
            for name in ("Lv04AnglersTunnel.lvb", "Lv04AnglersTunnel_02D.leb", "Lv04AnglersTunnel_04AL.leb",
                         "Lv04AnglersTunnel_04AU.leb", "Lv04AnglersTunnel_02D_x.leb", "Lv04Other_03C.leb", "Lv04AnglersTunnel_05E.leb"):
                (level / name).write_bytes(b"")
            self.assertEqual(self.images.level_cells(Path(romfs), "Lv04AnglersTunnel"), {"02D", "05E"})


# (block, block width, block height, RGBA) from the game's textures, RGBA by texture2ddecoder
BLOCKS = [
    ("fcfdffffffffffffffffffffffff0000", 4, 4, "ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00"),
    ("518203b3e015bc000044804824229188", 4, 4, "ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00bcc4c400c1c9c900c1c9c900bcc4c400"),
    ("528802d4ff5fffc75a67200000004078", 4, 4, "f7fffffff7ffffffe3eaeaffc6d6d610a6acacffa6acacffa6acacff9facac00a6acacffa6acacffa6acacff9facac00a6acacffa6acacffa6acacff9facac00"),
    ("4142516565ff01fe0138002c80082001", 4, 4, "a7b1b100a7b1b100a8b2b2ffa8b2b2ffa7b1b100a7b1b100a8b2b2ffa8b2b2ffa7b1b100a7b1b100a8b2b2c7a8b2b2ffa7b1b100a7b1b100a7b1b100a8b2b2ab"),
    ("fcfdffffffffffffffffffffffff0000", 8, 8, "ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00"),
    ("7191203446f8517c90067f7271000000", 8, 8, "32141400321414003214140032141400321414003214140032141400321414003214140032141400321414003214140032141400321414003214140032141400321414003214140032141400321414004c1e1e0d4c1e1e0d4c1e1e0d4c1e1e0d3214140032141400321414003f191906b2474740bf4c4c46bf4c4c46bf4c4c46321e1400452a1c185f392638fe6565bfff6666f9ff6666ffff6666ffff6666ff321e140072442e50b26b479fff6666f2ff6666ffff6666ffff6666ffff6666ff321e1400985c3d80df8659d7ff6666ffff6666ffff6666ffff6666ffff6666ff321e1400bf734cafff9966ffff6666ffff6666ffff6666ffff6666ffff6666ff"),
    ("f1291a746c51480819757d36c54fc259", 8, 8, "e35454ffe35454ff9d3a3ab1371b0028371b0028371b0031371b001c371b001ce35454ffe35454ff772c2c86371b0033371b0033371b0028371b002c371b002ce35454ffca4b4be34b1c1c55381c003c381c003d371b0025381c0038371b0035e35454ff6425257131121238381c003c381c0044371b0031371b0036371b0025e35454ff6425257131121238381c003c381c004b381c003f371b0036371b0025e35454ffca4b4be34b1c1c55381c003c381c0052381c004a381c0038371b0035e35454ffe35454ff973838aa381c0046381c0054381c004d381c0038381c0038e35454ffe35454ffe35454ff381c0054381c0054381c004d381c0038381c0038"),
    ("5d44a76e42b0df7277bff77377bb1789", 6, 6, "c6a15c00c6a15c00c6a15c00c6a15c00cba55e00d2ab6100c6a15ccbc6a15ccbc6a15ccbc6a15ccbcba55ecbd2ab61cbd2ab61cbcba55ecbc9a35dcbd0a960cbd2ab61cbd2ab61cbc6a15ccbbf9c58cbbd9a57cbc4a05acbc6a15ccbc6a15ccbd2ab61cbd2ab61cbd2ab61cbd2ab61cbcea75fcbc6a15ccbd2ab61cbd2ab61cbcea75fcbbf9c58cbbf9c58cbc6a15ccb"),
    ("4f40a55fcbfc0198292925a5a4946600", 6, 6, "d0ad6400d0ad6400d0ad6400d0ad6400d0ad6400d0ad6400d1ae6450d1ae6586d2af65acd2af65c6d2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65ccd2af65cc"),
    ("5f400f2b13022effffffffffbf790000", 6, 6, "c9a35d00c9a35d00c9a35d00c9a35d00c9a35d00c9a35d00c9a35d00cba55e49cda65f86cfa860bccea760accca65f76cda75f93cea760b6cfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cccfa860cc"),
    ("f240a14dc36e999981c624f0fcb5bdf9", 6, 6, "d0a661ccd0a661ccd0a661cccaa15eccc19a5accc19a5accc49c5bcccea460ccc19a5accc0995acccba25fccc19a5acccaa15eccc89f5dccbf9859ccbb9557ccbe9859ccc19a5accbf9859ccb38f53ccb08c52ccae8b51ccae8b51ccb69155cca6844dcc9a7b48cc997a47cc9d7e49cca3824ccca3824cccb28e53cca6844dccac8950ccac8950cca3824ccca3824ccc"),
    ("71a4a3f98dcbe9c6f6bb7753de3b7c9b", 6, 6, "a48653cca48653cca48653cca48653cca48653c9a48653c3a98b56cca38552cca18350cca48653cca68854cba98b56caa78955cca28451cca18350cca68854cca68854cca38552cca48653cca48653cca48653cca78955cca58754cca18351cca48653cca88a56cca98b56cca58753cca38552cca38552cca48653cca88a56cca88a56cca18351cc9f814fcc9f814fcc"),
    ("e2080f63f7f2aefac920a0893cfa203d", 10, 10, "a68536ffa68536ffa78637ffa78637ffa78637ffa78637ffa78637ffa78637ffa68536ffa68536ffa68536ffa68536ffa78637ffa78637ffa78637ffa78637ffa78637ffa78637ffa78637ffa68536ff806042ff7e5d3eff7c5939ff7a5536ff7a5536ff846649ff947e65ff9e8d75ff9d8b73ff9a886fff826345ff7e5c3dff7a5536ff775131ff754e2dff7f5e3fff927b61ffa09079ffa09079ffa09079ff826345ff826244ff805f40ff7c5a3aff765030ff7f5e3fff927b61ffa09079ffa09079ffa09079ff7f5e3fff806042ff826345ff7e5c3dff775131ff7d5b3cff8d7357ff9a876eff9a886fff9d8b73ff795434ff7d5b3cff826244ff7e5c3dff775131ff7a5536ff846548ff8d7357ff927a60ff968168ff765030ff7c5939ff806042ff7e5c3dff775131ff775131ff7d5b3cff826345ff886b4fff8e755aff7c5a3aff7f5e3fff826244ff7e5c3dff775131ff765030ff795434ff7c5939ff7e5d3eff816143ff826345ff826345ff826345ff7e5c3dff775131ff744d2cff744d2cff744d2cff744d2cff744d2cff"),
    ("a3045526753c4a4901d0f3d653bf5bce", 10, 10, "b7933fffab8635ffa17b2effa57f31ffb38e3cffb79345ffb7934effb79351ffb79348ffb7933fffa37d3affa57f35ffa78132ffa98334ffaf8a38ffb38e3fffb59148ffb7934effb79345ffb7933fff966e35ff9e7635ffa57f34ffab8635ffab8635ffaf8a3cffb38e45ffb7934affb79345ffb7933fff825831ff966e35ffab8638ffad8837ffa78132ffab8635ffb38e3fffb79345ffb79342ffb7933fff73462eff8e6534ffa9833affaf8a38ffa57f31ffa78132ffb18c3cffb79342ffb79340ffb7933fff673929ff845a2fffa17b37ffab863cffa98338ffa98338ffb38e42ffbb9746ffbb9746ffbb9746ff623324ff7b4f2bff926931ffa17b3cffa78140ffb18c48ffb9954effbe9b51ffbe9b51ffbe9b51ff5c2c20ff6b3d24ff794d27ff926938ffab864dffbd995bffc09e5effc4a261ffc4a261ffc4a261ff56251bff5c2c1dff653720ff825837ffb79361ffc6a46dffc8a66fffcaa970ffcaa970ffcaa970ff522118ff522118ff522118ff794d37ffb79368ffcead7bffcead7bffcead7bffcead7bffcead7bff"),
    ("45300a77de59e9f615b72a80fbfb0383", 10, 10, "ba8b45ffac8040ffa87d3effa87d3effa87d3effa87d3eff96704aff9b7c57ffa28b68ffa28b68ffb48743ffab803fffa87d3effa87d3effa87d3eff7f5c2eff96704aff9b7c57ffa28b68ffa28b68ffae8140ffa97e3effa87d3effa87d3eff7f5c2eff7f5c2eff96704aff9b7c57ffa28b68ffa28b68ffa87d3effa87d3effa87d3eff7f5c2eff7f5c2eff7f5c2eff96704aff9b7c57ffa28b68ffa28b68ffae8140ffae8140ff8a7244ff8a7244ff8a7244ff8a7244ff97734eff9b7c57ffa28b68ffa28b68ffb48743ff978c5eff978c5eff978c5eff978c5eff978c5eff987550ff9b7c57ffa28b68ffa28b68ffa28b68ffa2a274ffa2a274ffa2a274ffa2a274ffa2a274ff9a7853ff9b7c57ffa28b68ffa28b68ffa28b68ffa2a274ffa2a274ffa2a274ffa2a274ffa2a274ff9a7853ff9b7c57ffa28b68ffa28b68ffa28b68ffa2a274ffa2a274ffa2a274ffa2a274ffa2a274ff9a7853ff9b7c57ffa28b68ffa28b68ffa28b68ffa2a274ffa2a274ffa2a274ffa2a274ffa2a274ff9a7853ff9b7c57ffa28b68ffa28b68ff"),
    ("d124df5c7f56bb9a00800088bf890089", 10, 10, "6f4e2fff6f4e2fff6f4e2fff6f4e2fff6f4e2fff6c4b2fff65452fff61412fff61412fff61412fff6f4e2fff6f4e2fff6f4e2fff6f4e30ff6f4e32ff6d4c33ff6b4a35ff6a4936ff6a4936ff6a4936ff6f4e2fff6f4e2fff6f4e2fff6f4e31ff6f4e35ff715039ff73523cff74533eff74533eff74533eff6f4e2fff6f4e2fff6f4e2fff6f4e32ff6f4e38ff73513dff795742ff7d5b45ff7d5b45ff7d5b45ff6f4e2fff6f4e2fff6f4e2fff6d4c31ff6c4b35ff6d4c39ff72503cff74533eff74533eff74533eff6f4e2fff6f4e2fff6f4e2fff6c4c30ff674732ff664633ff684835ff6a4936ff6a4936ff6a4936ff6f4e2fff6f4e2fff6f4e2fff6b4a2fff64432fff61412fff61412fff61412fff61412fff61412fff6f4e2fff6d4c2fff6c4b2fff68482fff63432fff61412fff61412fff61412fff61412fff61412fff6f4e2fff6b4a2fff66462fff64432fff62422fff61412fff61412fff61412fff61412fff61412fff6f4e2fff69482fff63432fff61412fff61412fff61412fff61412fff61412fff61412fff61412fff"),
    ("96c4440bea500a28116a5a2920408879", 10, 10, "8a5941ff7a4e39ff592f22ff52281dff47281dff47281dff4b281dff482e22ff65412fff65412fff8a5941ff7a4d38ff593023ff52291eff48281dff48291eff4b291eff482e22ff65412fff65412fff8a5941ff7a4b37ff593023ff52291eff4a281dff4a2a1eff4b2a1eff482e22ff65412fff65412fff8a5941ff7a4b37ff593124ff522a1eff4b281dff4b2a1eff4b2a1fff482e22ff65412fff65412fff8a5941ff7a4a36ff593325ff522a1eff4c281dff4c2a1fff4b2b1fff482e22ff65412fff65412fff8a5941ff7a4835ff593325ff522a1fff4d281dff4d2b1fff4b2d20ff482e22ff65412fff65412fff8a5941ff7a4734ff593426ff522a1fff4e281dff4e2c20ff4b2d21ff482e22ff65412fff65412fff8a5941ff7a4633ff593527ff522b1fff4f281dff4f2c20ff4b2d21ff482e22ff65412fff65412fff8a5941ff7a4532ff593527ff522b1fff51281dff512d20ff4b2f22ff482e22ff65412fff65412fff8a5941ff7a4431ff593728ff522c20ff52281dff522d21ff4b3023ff482e22ff65412fff65412fff"),
    ("f1c476c2d11312e6124e12ce123a123a", 10, 10, "76522bff76522bff76522bff686233ff517b41ff498546ff498546ff498546ff498546ff498546ff76522bff76522bff76522bff686233ff517b41ff498546ff498546ff498546ff498546ff498546ff76522bff76522bff76522bff686233ff517b41ff498546ff498546ff498546ff498546ff498546ff6f522bff6c582eff695f32ff606c39ff4f7f43ff498546ff498546ff498546ff498546ff498546ff64522bff5b6535ff54753eff4f7f43ff4c8244ff498546ff498546ff498546ff498546ff498546ff5a5f32ff536c39ff4c783fff498043ff498345ff498546ff498546ff498546ff498546ff498546ff50753eff4d733dff4a723cff49783fff498244ff498546ff498546ff498546ff498546ff498546ff498244ff497a40ff49723cff49753eff498043ff498546ff498546ff498546ff498546ff498546ff49773eff49783fff497a40ff497d42ff498345ff498546ff498546ff498546ff498546ff498546ff496c39ff49773eff498244ff498546ff498546ff498546ff498546ff498546ff498546ff498546ff"),
    ("340506e680ad2da086b78b3f6851ff7f", 10, 10, "737373ff737373ff737373ff737373ff737373ff737373ff737373ff737373ff5e5e5eff3b3b3bff737373ff737373ff737373ff737373ff737373ff737373ff737373ff3b3b3bff1c1c1cff111111ff545454ff696969ff696969ff545454ff545454ff494949ff343434ff111111ff030303ff030303ff4d4d4dff696969ff696969ff4d4d4dff4d4d4dff3b3b3bff151515ff0a0a0aff0a0a0aff151515ff737373ff707070ff6c6c6cff6c6c6cff707070ff656565ff424242ff1c1c1cff1f1f1fff424242ff737373ff505050ff424242ff424242ff656565ff707070ff6c6c6cff464646ff494949ff6c6c6cff4d4d4dff262626ff151515ff151515ff1f1f1fff3b3b3bff737373ff4d4d4dff424242ff4d4d4dff232323ff0e0e0eff030303ff030303ff030303ff1f1f1fff5b5b5bff464646ff3b3b3bff3b3b3bff111111ff111111ff111111ff111111ff111111ff1f1f1fff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff3b3b3bff"),
    ("ba80d75ffc271f00944aa510281402f9", 10, 10, "fffffb00fffffb00fffffb00fffffb00fffffb00fffffb00fffffb00fffffb00fffffb00fffffb00cab28900c5ab7e00c2a77900bda06e00bb9c6800b8996300b8996300b8996300b8996300b8996300c2a77900bda06e00b99a6500b4935b00b18e5200b18e5200b3915800b7976000bc9e6b00bea27000c2a77900bda06e00b99a6500b4935b00b18e5200b18e5200b4935b00b99a6500bea27000c2a77900c2a77900bda06e00b99a6500b4935b00b18e5200af8c5000b18e5200b3915800b6955d00b7976000c2a77900bda06e00b99a6500b4935b00b18e5200af8c5000b18e5200b3915800b6955d00b7976000c2a77900c0a47300bda06e00b99a6500b7976000b7976000b99a6500bda06e00c0a47300c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900c2a77900"),
    ("daa0fbb811108c0527bd99a9e47afb30", 10, 10, "cebf8d00ccba8600cab47f00c9af7800c8ae7600c8ac7300c7aa7100c9b17a00ccb88400cebf8d00ccba8700cdbb8700cdbd8a00cdbd8a00cdbb8800ccb98600ccb88300ccb98500ccb98500ccba8700ccba8600ccb98500cbb78300cbb68200cbb78300ccb98500ccba8600cbb78200cab47e00c9b17b00cbb68100cbb58000cbb58000cab47f00cbb58000cbb58000cbb68100cbb57f00cab47f00cab37d00cab47f00cab47e00cab37c00cab27b00c9b17a00c9b07800c9af7700c9af7700c9af7700c9af7700ccb98500cbb78200cab47e00cab27b00c9b17b00c9b17b00c9b17a00cab27b00cab37c00cab37d00cebf8d00ccba8700cbb58000c9b17a00cab47e00cbb78200ccba8600ccb98600ccb98600ccb98500ccba8600cbb58000c9b17b00c8ad7500c9b17a00cab47f00ccb88400cbb58000c9b17b00c8ae7600c8ad7500c8ae7700cab27c00cab47e00cab47e00cab47e00cab37d00c9b17b00c8ad7500c7ab7300cebf8d00ccba8600cab47f00c9af7800c8ae7600c8ac7300c7aa7100c8ad7500c9b17b00cab47f00"),
    ("fe4067f5640030fe92e616e1f527e1ba", 10, 10, "c5a56d19c7a66d0ec8a76e03c6a66d0fc4a46c28c4a46c25c6a66d0fc8a76e00c8a76e00c8a76e00c2a26a3bc3a36b30c4a46c25c4a46c28c3a36b2ec2a26b38c1a26a41c1a26a43c2a26a3bc3a36b33c2a26a3bc3a36b30c4a46c25c4a46c28c3a36b2ec2a26b39c1a16a46c0a16a4ac1a26a43c2a26a3bc5a56d19c7a66d0ec8a76e03c6a66d0fc4a46c28c4a36b2bc5a46c20c5a56d19c5a56d19c5a56d19c0a1694cc2a26b39c4a46c27c3a36b32c0a16a4ac1a16a49c3a36b36c4a46c28c5a46c20c5a56d19c0a06954c1a26a41c3a36b2ec3a36b36c0a1694cc0a1694cc2a26a3fc2a26b38c3a36b30c4a46c28c3a36b32c4a46c27c5a56c1cc5a46c20c3a36b2dc2a26b38c1a26a43c0a16a4ac0a16a4ac0a16a4ac1a26a43c2a26b38c3a36b2dc3a36b36c0a1694cc1a16a47c3a36b33c4a46c27c5a46c1fc6a56d17c0a16a4ac2a26a3fc3a36b35c2a26a3ec0a06952c0a06954c2a26a3fc3a36b33c4a36b2bc4a46c25c0a16a4ac2a26a3fc3a36b35c2a26b39c1a16a46c0a06950bfa0685bbe9f6863be9f6863be9f6863"),
    ("fe85fbe8e28f3efbece373a9d02a13c2", 10, 10, "cdb78858ccb68658cbb58458cab48258c9b38158c8b28058c8b28058c8b28058c8b28058c8b28058cdb78864ccb68664cbb58464cab48264c9b38164c8b28064c9b38164c9b38264cab48364cab48364cdb78871ccb68671cbb58471cab48271c9b38171c9b38171c9b38271cbb58471ccb68671cdb78771c8b28075c8b28075c8b28075c8b27f75c8b27f75c8b28075c9b38175cab48375cbb58475ccb68675c0aa7375c2ac7675c4ae7975c6b07c75c7b17d75c8b28075c8b28075c9b38175c9b38175c9b38175c1ab7575c3ad7875c5af7a76c7b17d77c8b28078c8b28078c8b28078c7b17e78c6b07d78c5af7b78c8b28075c9b38177cab48279cab4837ccbb5847dcbb5847ec9b3817ec6b07c7ec2ac777ec0aa737ecdb78778cdb7887bcdb7887dcdb78881ceb88983ccb68685cab48285c6b07c85c2ac7685bfa97185cab48385ccb68686cdb78887cfb98a89d0ba8c8acfb98b8bcdb7888bcbb5848bc8b2808bc6b07d8bc8b28091cbb58491cdb78791d0ba8c91d2bc8f91d2bc9091d1bb8e91d0ba8c91cfb98a91cdb78891"),
    ("fda097aecc49a3433fad6dbdad7a0000", 8, 8, "694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe694f29fe7d6435fe7d6435fe7b6234fe6e552cfe6e552cfe7b6234fe7d6435fe7d6435fe7e6535fe7e6535fe7c6334fe755b30fe755b30fe7c6334fe7e6535fe7e6535fe785e32fe785e32fe785e32fe785e32fe796033fe7d6435fe7e6535fe7e6535fe7b6234fe796033fe785e32fe785e32fe7b6234fe816737fe826938fe826938fe846b39fe7d6435fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe785e32fe"),
]
