"""The ghost relay (grouping, the area filter, off messages) with fake clients, and the client's link to it."""
import asyncio
import struct
import unittest
from typing import List, Optional, Tuple

from .. import ghost_relay as wire

FIELD = wire.LEVEL_IDS["Field"]
SHOP = wire.LEVEL_IDS["Shop"]
QUIET = 0.15  # how long a test waits to be sure nothing arrives


class FakeClient:
    def __init__(self, port: int):
        self.port = port
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self.id = -1
        self.seq = 0

    async def join(self, seed: str, password: str, slot: str, version: int = wire.PROTOCOL_VERSION) -> bytes:
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", self.port)
        message = bytearray(wire.join_message(wire.group_id(seed, password), slot, slot + " (player)"))
        message[1] = version
        self.writer.write(wire.frame(bytes(message)))
        first = await self.next()
        if first[0] == wire.WELCOME:
            self.id = struct.unpack_from(">H", first, 2)[0]
        return first

    async def next(self, timeout: float = 2) -> bytes:
        return await asyncio.wait_for(wire.read_frame(self.reader), timeout)

    async def collect(self, seconds: float = QUIET) -> List[bytes]:
        got = []
        try:
            while True:
                got.append(await self.next(seconds))
        except asyncio.TimeoutError:
            return got

    def pos(self, level: int, x: float, z: float, y: float = 0.0) -> None:
        self.seq += 1
        body = wire.POS_BODY.pack(self.seq, level, wire.NO_ROOM, x, y, z, 0, 0, 0, 0)
        self.writer.write(wire.frame(bytes([wire.POS]) + body))

    def nowhere(self) -> None:
        self.writer.write(wire.frame(bytes([wire.NOWHERE])))

    def close(self) -> None:
        self.writer.close()


def frames(messages: List[bytes]) -> List[Tuple[int, float, float]]:
    """(player id, x, z) of each FRAME."""
    out = []
    for m in messages:
        if m[0] == wire.FRAME:
            body = wire.POS_BODY.unpack(m[3:])
            out.append((struct.unpack_from(">H", m, 1)[0], body[3], body[5]))
    return out


def offs(messages: List[bytes]) -> List[Tuple[int, int]]:
    return [struct.unpack_from(">HB", m, 1) for m in messages if m[0] == wire.OFF]


def field(col: int, row: int) -> Tuple[float, float]:
    """The middle of overworld screen Field_<row><col> (col A = 0), as TESTING.md places it."""
    return 15 * col + 7.5, 12 * (row - 1) + 6.75


class TestRelay(unittest.TestCase):
    def run_relay(self, test) -> None:
        async def main() -> None:
            relay = wire.Relay()
            server = await asyncio.start_server(relay.handle, "127.0.0.1", 0)
            port = server.sockets[0].getsockname()[1]
            try:
                await test(port, relay)
            finally:
                server.close()
        asyncio.run(main())

    def test_screens(self) -> None:
        self.assertEqual(wire.field_screen(*field(0, 1)), (0, 1))
        self.assertEqual(wire.field_screen(38.25, 128.25), (2, 11))  # Mabe at Marin's door: Field_11C
        self.assertEqual(wire.field_screen(-3, 500), (0, 16))
        self.assertEqual(wire.room_index("Field", *field(1, 2)), 17)
        self.assertEqual(wire.room_index("Shop", 1, 1), wire.NO_ROOM)

    def test_groups_and_area(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            a, b, c = FakeClient(port), FakeClient(port), FakeClient(port)
            self.assertEqual((await a.join("1234abcd", "pw", "A"))[0], wire.WELCOME)
            self.assertEqual((await b.join("1234abcd", "pw", "B"))[0], wire.WELCOME)
            # another password: another group, though the seed is the same
            self.assertEqual((await c.join("1234abcd", "other", "C"))[0], wire.WELCOME)
            players = [m for m in await a.collect() if m[0] == wire.PLAYER]
            self.assertEqual([wire.read_name(m, 3)[0] for m in players], ["B"])
            self.assertEqual([wire.read_name(m, 3)[0] for m in await b.collect() if m[0] == wire.PLAYER], ["A"])
            self.assertEqual(await c.collect(), [])

            # A and C in the same screen, B two screens east of A: nobody sees A
            a.pos(FIELD, *field(2, 11))
            c.pos(FIELD, *field(2, 11))
            b.pos(FIELD, *field(4, 11))
            self.assertEqual(frames(await b.collect()), [])
            self.assertEqual(await c.collect(), [])
            self.assertEqual(frames(await a.collect()), [])

            # B steps into the screen next to A's: B gets A's last frame at once, A gets B's
            b.pos(FIELD, *field(3, 12))
            self.assertEqual(frames(await b.collect()), [(a.id, *field(2, 11))])
            self.assertEqual(frames(await a.collect()), [(b.id, *field(3, 12))])
            self.assertEqual(await c.collect(), [])

            # moving within the area: frames go on
            a.pos(FIELD, 40, 130)
            a.pos(FIELD, 41, 130)
            self.assertEqual(frames(await b.collect()), [(a.id, 40, 130), (a.id, 41, 130)])

            # A walks two screens away: each gets an off for the other
            a.pos(FIELD, *field(1, 11))
            self.assertEqual(offs(await b.collect()), [(a.id, wire.OFF_AREA)])
            self.assertEqual(offs(await a.collect()), [(b.id, wire.OFF_AREA)])

            # same interior: seen wherever in it
            a.pos(SHOP, 1, 1)
            b.pos(SHOP, 9, 7)
            self.assertEqual(frames(await a.collect()), [(b.id, 9, 7)])
            self.assertEqual(frames(await b.collect()), [(a.id, 1, 1)])

            # a level load (no level): off
            a.nowhere()
            self.assertEqual(offs(await b.collect()), [(a.id, wire.OFF_AREA)])
            self.assertEqual(offs(await a.collect()), [(b.id, wire.OFF_AREA)])
            a.pos(SHOP, 2, 2)
            self.assertEqual(frames(await b.collect()), [(a.id, 2, 2)])
            await a.collect()

            # leaving the group: off with the reason
            a.close()
            self.assertEqual(offs(await b.collect()), [(a.id, wire.OFF_LEFT)])
            for client in (b, c):
                client.close()
            await asyncio.sleep(0.05)
            self.assertEqual(relay.groups, {})
        self.run_relay(test)

    def test_same_slot_again(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            a, b, a2 = FakeClient(port), FakeClient(port), FakeClient(port)
            await a.join("1234abcd", "", "A")
            await b.join("1234abcd", "", "B")
            a.pos(SHOP, 1, 1)
            b.pos(SHOP, 1, 1)
            await a.collect()
            await b.collect()
            # A reconnects before the relay noticed the old connection is gone: the old one is closed
            await a2.join("1234abcd", "", "A")
            got = await b.collect()
            self.assertEqual(offs(got), [(a.id, wire.OFF_LEFT)])
            self.assertEqual([wire.read_name(m, 3)[0] for m in got if m[0] == wire.PLAYER], ["A"])
            with self.assertRaises(asyncio.IncompleteReadError):
                await a.next()
            self.assertEqual(len(relay.groups[wire.group_id("1234abcd", "")]), 2)
            for client in (b, a2):
                client.close()
        self.run_relay(test)

    def test_refusals(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            old = FakeClient(port)
            reply = await old.join("1234abcd", "", "A", version=wire.PROTOCOL_VERSION + 1)
            self.assertEqual(reply[0], wire.ERROR)
            with self.assertRaises(asyncio.IncompleteReadError):
                await old.next()
            # a frame that is not a join
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            writer.write(wire.frame(bytes([wire.PING])))
            self.assertEqual((await asyncio.wait_for(wire.read_frame(reader), 2))[0], wire.ERROR)
            writer.close()
            # an oversized frame closes the connection
            a = FakeClient(port)
            await a.join("1234abcd", "", "A")
            a.writer.write(struct.pack(">H", wire.MAX_FRAME + 1) + bytes(wire.MAX_FRAME + 1))
            with self.assertRaises(asyncio.IncompleteReadError):
                await a.next()
            self.assertEqual(relay.members, {})
        self.run_relay(test)

    def test_silent_member_is_hidden(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            a, b = FakeClient(port), FakeClient(port)
            await a.join("1234abcd", "", "A")
            await b.join("1234abcd", "", "B")
            a.pos(SHOP, 1, 1)
            b.pos(SHOP, 1, 1)
            await a.collect()
            await b.collect()
            member = relay.members[a.id]
            member.last_pos -= wire.SILENT_OFF + 1
            sweep = asyncio.create_task(relay.sweep())
            self.assertEqual(offs(await b.collect(1.5)), [(a.id, wire.OFF_AREA)])
            sweep.cancel()
            a.close()
            b.close()
        self.run_relay(test)


class TestGhostLink(unittest.TestCase):
    def test_pos_lines_become_ghost_lines(self) -> None:
        from ..ghosts import GhostLink, pos_body

        self.assertIsNone(pos_body("POS none".split(), 0))
        self.assertIsNone(pos_body("POS NoSuchLevel 255 1 2 3 0 0 0".split(), 0))

        async def main() -> None:
            relay = wire.Relay()
            server = await asyncio.start_server(relay.handle, "127.0.0.1", 0)
            address = f"127.0.0.1:{server.sockets[0].getsockname()[1]}"
            lines = {"A": [], "B": []}
            links = {name: GhostLink(lines[name].append) for name in lines}
            exit_event = asyncio.Event()
            tasks = []
            for name, link in links.items():
                link.on_game_connected()
                link.configure(True, address, "pw", "1234abcd", name, name)
                tasks.append(asyncio.create_task(link.run(exit_event)))

            async def until(test, seconds: float = 3) -> None:
                for _ in range(int(seconds / 0.02)):
                    if test():
                        return
                    await asyncio.sleep(0.02)
                self.fail(f"timed out; lines: {lines}")

            await until(lambda: all("GHOSTS 1" in lines[n] for n in lines) and len(links["A"].players) == 1)
            links["A"].on_pos("POS Field 170 38.250 10.500 128.250 90.0 0 0 1".split())
            links["B"].on_pos("POS Field 171 50.000 10.500 128.250 270.0 0 0".split())
            await until(lambda: any(l.startswith("GHOST 0 ") for l in lines["B"]))
            self.assertIn("GHOST 0 Field 38.250 10.500 128.250 90.0 0 0 1 A", lines["B"])
            await until(lambda: "GHOST 0 Field 50.000 10.500 128.250 270.0 0 0 0 B" in lines["A"])
            # rate: a burst of positions goes out at most every MIN_INTERVAL, the last one included
            for i in range(10):
                links["A"].on_pos(f"POS Field 170 {39 + i} 10.5 128.25 90 0 0".split())
            await until(lambda: "GHOST 0 Field 48.000 10.500 128.250 90.0 0 0 0 A" in lines["B"])
            self.assertLess(sum(l.startswith("GHOST 0 Field 4") for l in lines["B"]), 5)
            # a new animation id is not held back by the rate
            links["A"].on_pos("POS Field 170 48 10.5 128.25 90 0 0".split())
            links["A"].on_pos("POS Field 170 48 10.5 128.25 90 7 0".split())
            links["A"].on_pos("POS Field 170 48 10.5 128.25 90 7 40".split())
            await until(lambda: "GHOST 0 Field 48.000 10.500 128.250 90.0 7 0 0 A" in lines["B"])
            await until(lambda: "GHOST 0 Field 48.000 10.500 128.250 90.0 7 40 0 A" in lines["B"])
            # A goes inside: B's ghost of A turns off
            links["A"].on_pos("POS Shop 255 1 0 1 0 0 0".split())
            await until(lambda: "GHOST 0 off" in lines["B"])
            # ghosts turned off: A's link leaves the group and tells the game
            links["A"].configure(False, address, "pw", "1234abcd", "A", "A")
            await until(lambda: links["A"].status.text == "Ghosts are off." and not links["B"].players)
            self.assertEqual(lines["A"][-2:], ["GHOST 0 off", "GHOSTS 0"])
            exit_event.set()
            for link in links.values():
                link.wake.set()
            await asyncio.gather(*tasks)
            server.close()
        asyncio.run(main())
