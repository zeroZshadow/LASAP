"""The ghost relay (grouping, the area filter, off messages) with fake clients, and the client's link to it."""
import asyncio
import ipaddress
import struct
import unittest
from typing import List, Optional, Tuple
from unittest import mock

from .. import ghost_relay as wire

FIELD = wire.LEVEL_IDS["Field"]
SHOP = wire.LEVEL_IDS["Shop"]
QUIET = 0.15  # how long a test waits to be sure nothing arrives


class FakeClient:
    def __init__(self, port: int, websocket: bool = False):
        self.port = port
        self.websocket = websocket
        self.reader = None  # asyncio.StreamReader or wire.WebSocketReader
        self.writer = None  # asyncio.StreamWriter or wire.WebSocketWriter
        self.id = -1
        self.seq = 0

    async def join(self, secret: str, slot: str, version: int = wire.PROTOCOL_VERSION,
                   password: Optional[str] = "") -> bytes:
        """Joins; password None sends a join without the password field."""
        _, self.reader, self.writer = await wire.connect(
            f"{'ws://' if self.websocket else ''}127.0.0.1:{self.port}")
        message = bytearray(wire.join_message(wire.group_id(secret), slot, slot + " (player)", password or ""))
        if password is None:
            message = message[:-1]
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
        body = wire.Pos(self.seq, level, x, y, z, 0, 0, 0, 0, 0, wire.SHAPES_DEFAULT, wire.TUNIC_GREEN, 0, 0, 0).pack()
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
            pos = wire.Pos.unpack(m[3:])
            out.append((struct.unpack_from(">H", m, 1)[0], pos.x, pos.z))
    return out


def offs(messages: List[bytes]) -> List[Tuple[int, int]]:
    return [struct.unpack_from(">HB", m, 1) for m in messages if m[0] == wire.OFF]


def players(messages: List[bytes]) -> List[Tuple[int, str]]:
    """(player id, slot name) of each PLAYER."""
    return [(struct.unpack_from(">H", m, 1)[0], wire.read_name(m, 3)[0]) for m in messages if m[0] == wire.PLAYER]


def field(col: int, row: int) -> Tuple[float, float]:
    """The middle of overworld screen Field_<row><col> (col A = 0), as TESTING.md places it."""
    return 15 * col + 7.5, 12 * (row - 1) + 6.75


class TestRelay(unittest.TestCase):
    def run_relay(self, test, echo: Optional[float] = None, password: str = "", nogroups: bool = False) -> None:
        async def main() -> None:
            relay = wire.Relay(echo, password, nogroups)
            server = await asyncio.start_server(relay.handle, "127.0.0.1", 0)
            port = server.sockets[0].getsockname()[1]
            try:
                await test(port, relay)
            finally:
                server.close()
        asyncio.run(main())

    def test_echo(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            a = FakeClient(port)
            await a.join("secret1", "A")
            players = [m for m in await a.collect() if m[0] == wire.PLAYER]
            self.assertEqual([(struct.unpack_from(">H", m, 1)[0], wire.read_name(m, 3)[0]) for m in players],
                             [(wire.ECHO_ID, "Echo")])
            # each frame comes back as the echo's, along x; leaving the level turns the echo off
            a.pos(FIELD, *field(2, 11))
            x, z = field(2, 11)
            self.assertEqual(frames(await a.collect()), [(wire.ECHO_ID, x + 2, z)])
            a.nowhere()
            self.assertEqual(offs(await a.collect()), [(wire.ECHO_ID, wire.OFF_AREA)])
            a.close()
        self.run_relay(test, echo=2.0)

    def test_screens(self) -> None:
        self.assertEqual(wire.field_screen(*field(0, 1)), (0, 1))
        self.assertEqual(wire.field_screen(38.25, 128.25), (2, 11))  # Mabe at Marin's door: Field_11C
        self.assertEqual(wire.field_screen(-3, 500), (0, 16))

    def test_groups_and_area(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            a, b, c = FakeClient(port), FakeClient(port), FakeClient(port)
            self.assertEqual((await a.join("secret1", "A"))[0], wire.WELCOME)
            self.assertEqual((await b.join("secret1", "B"))[0], wire.WELCOME)
            # another secret (another multiworld): another group
            self.assertEqual((await c.join("secret2", "C"))[0], wire.WELCOME)
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
            await a.join("secret1", "A")
            await b.join("secret1", "B")
            a.pos(SHOP, 1, 1)
            b.pos(SHOP, 1, 1)
            await a.collect()
            await b.collect()
            # A reconnects before the relay noticed the old connection is gone: the old one is closed
            await a2.join("secret1", "A")
            got = await b.collect()
            self.assertEqual(offs(got), [(a.id, wire.OFF_LEFT)])
            self.assertEqual([wire.read_name(m, 3)[0] for m in got if m[0] == wire.PLAYER], ["A"])
            with self.assertRaises(asyncio.IncompleteReadError):
                await a.next()
            self.assertEqual(len(relay.groups[wire.group_id("secret1")]), 2)
            for client in (b, a2):
                client.close()
            # alone in its group: the new connection is in the group again, where the next player finds it
            c, c2, d = FakeClient(port), FakeClient(port), FakeClient(port)
            await c.join("secret3", "C")
            await c2.join("secret3", "C")
            await d.join("secret3", "D")
            self.assertEqual(players(await d.collect()), [(c2.id, "C")])
            for client in (c2, d):
                client.close()
        self.run_relay(test)

    def test_nogroups(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            # two multiworlds, each with a slot Link
            a, b, c = FakeClient(port), FakeClient(port), FakeClient(port)
            await a.join("secret1", "Link")
            await b.join("secret2", "Link")
            await c.join("secret2", "C")
            got = {client: await client.collect() for client in (a, b, c)}
            self.assertEqual(sorted(players(got[a])), [(b.id, "Link"), (c.id, "C")])
            self.assertEqual(sorted(players(got[b])), [(a.id, "Link"), (c.id, "C")])
            self.assertEqual(sorted(players(got[c])), [(a.id, "Link"), (b.id, "Link")])
            self.assertEqual(list(relay.groups), [wire.ONE_GROUP])

            # all in one level: each gets the frames of the others, never its own
            for client in (a, b, c):
                client.pos(SHOP, 1, 1)
            for client in (a, b, c):
                ids = [p for p, _, _ in frames(await client.collect())]
                self.assertEqual(sorted(set(ids)), sorted({a.id, b.id, c.id} - {client.id}))

            # the second multiworld's Link reconnects: it replaces its old connection, not the first one's Link
            b2 = FakeClient(port)
            await b2.join("secret2", "Link")
            self.assertEqual(sorted(players(await b2.collect())), [(a.id, "Link"), (c.id, "C")])
            got = await a.collect()
            self.assertEqual(offs(got), [(b.id, wire.OFF_LEFT)])
            self.assertEqual(players(got), [(b2.id, "Link")])
            with self.assertRaises(asyncio.IncompleteReadError):
                await b.next()
            b2.pos(SHOP, 1, 1)
            self.assertEqual(sorted({p for p, _, _ in frames(await b2.collect())}), sorted({a.id, c.id}))
            self.assertEqual(frames(await a.collect()), [(b2.id, 1, 1)])
            self.assertEqual(len(relay.groups[wire.ONE_GROUP]), 3)

            # full: only a slot already in it gets in again
            with mock.patch.object(wire, "MAX_GROUP", 3):
                d = FakeClient(port)
                reply = await d.join("secret3", "Link")
                self.assertEqual((reply[0], reply[1:].decode()), (wire.ERROR, "The ghost relay is full."))
                c2 = FakeClient(port)
                self.assertEqual((await c2.join("secret2", "C"))[0], wire.WELCOME)
            for client in (a, b2, c2):
                client.close()
            await asyncio.sleep(0.05)
            self.assertEqual(relay.groups, {})
        self.run_relay(test, nogroups=True)

    def test_refusals(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            old = FakeClient(port)
            reply = await old.join("secret1", "A", version=wire.PROTOCOL_VERSION + 1)
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
            await a.join("secret1", "A")
            a.writer.write(struct.pack(">H", wire.MAX_FRAME + 1) + bytes(wire.MAX_FRAME + 1))
            with self.assertRaises(asyncio.IncompleteReadError):
                await a.next()
            self.assertEqual(relay.members, {})
        self.run_relay(test)

    def test_websocket_beside_tcp(self) -> None:
        """A WebSocket client and a plain TCP one on the same port see each other; the relay answers pings and a
        close, and turns away an HTTP request that is no upgrade and an oversized WebSocket frame."""
        async def test(port: int, relay: wire.Relay) -> None:
            a, b = FakeClient(port, websocket=True), FakeClient(port)
            self.assertEqual((await a.join("secret1", "A"))[0], wire.WELCOME)
            self.assertIsInstance(a.reader, wire.WebSocketReader)
            await b.join("secret1", "B")
            self.assertEqual(players(await a.collect()), [(b.id, "B")])
            a.pos(SHOP, 1, 2)
            b.pos(SHOP, 3, 4)
            self.assertEqual(frames(await a.collect()), [(b.id, 3, 4)])
            self.assertEqual(frames(await b.collect()), [(a.id, 1, 2)])
            # a WebSocket ping is answered with a pong of the same data
            a.writer.send(wire.WS_PING, b"hi")
            head = await asyncio.wait_for(a.reader.reader.readexactly(4), 2)
            self.assertEqual(head, bytes([0x80 | wire.WS_PONG, 2]) + b"hi")
            # a close leaves the group
            a.close()
            self.assertEqual(offs(await b.collect()), [(a.id, wire.OFF_LEFT)])
            self.assertEqual(list(relay.members), [b.id])
            # a frame bigger than MAX_WS_MESSAGE closes the connection
            c = FakeClient(port, websocket=True)
            await c.join("secret1", "C")
            c.writer.writer.write(bytes([0x80 | wire.WS_BINARY, 0x80 | 127]) +
                                  struct.pack(">Q", wire.MAX_WS_MESSAGE + 1) + bytes(4))
            with self.assertRaises(asyncio.IncompleteReadError):
                while True:
                    await c.next()
            # a browser gets a page, not a connection
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            writer.write(b"GET / HTTP/1.1\r\nHost: relay.example\r\n\r\n")
            answer = await asyncio.wait_for(reader.read(), 2)
            self.assertTrue(answer.startswith(b"HTTP/1.1 426 "))
            self.assertIn(b"LAS ghost relay", answer)
            writer.close()
            b.close()
        self.run_relay(test)

    def test_websocket_from_a_proxy(self) -> None:
        """A WebSocket from this computer (cloudflared) or a --trusted-proxy counts as the address in CF-Connecting-IP
        or X-Forwarded-For, so MAX_PER_ADDRESS counts players, not the proxy."""
        self.assertEqual(wire.proxied_address({"cf-connecting-ip": "203.0.113.5", "x-forwarded-for": "172.18.0.3"}),
                         "203.0.113.5")
        self.assertEqual(wire.proxied_address({"x-forwarded-for": "10.0.0.1, 203.0.113.6"}), "203.0.113.6")
        self.assertEqual(wire.proxied_address({}), "")
        self.assertEqual(wire.parse_proxies(["172.18.0.0/16, caddy", "", "10.0.0.7"]),
                         [ipaddress.ip_network("172.18.0.0/16"), "caddy", ipaddress.ip_network("10.0.0.7/32")])

        async def trusts() -> None:
            relay = wire.Relay(trusted_proxies=wire.parse_proxies(["172.18.0.0/16", "caddy"]))
            for address in ("127.0.0.1", "::1", "::ffff:127.0.0.1", "172.18.0.9"):
                self.assertTrue(await relay.trusts(address), address)
            # a host name is looked up for each connection; anyone else is not trusted
            with mock.patch("socket.getaddrinfo", return_value=[(2, 1, 6, "", ("192.168.5.2", 0))]) as lookup:
                self.assertTrue(await relay.trusts("192.168.5.2"))
                self.assertFalse(await relay.trusts("198.51.100.9"))
            self.assertEqual(lookup.call_args[0][0], "caddy")
            self.assertFalse(await wire.Relay().trusts("172.18.0.9"))
        asyncio.run(trusts())

        async def open_from(port: int, player: str) -> Tuple[wire.WebSocketReader, wire.WebSocketWriter]:
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            writer.write(f"GET / HTTP/1.1\r\nHost: relay.example\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         f"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
                         f"CF-Connecting-IP: {player}\r\n\r\n".encode())
            line, headers = await wire.read_http_head(reader)
            self.assertEqual(headers["sec-websocket-accept"], "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")  # RFC 6455's example
            ws_writer = wire.WebSocketWriter(writer, client=True)
            return wire.WebSocketReader(reader, ws_writer), ws_writer

        async def test(port: int, relay: wire.Relay) -> None:
            joined, writers = [], []
            for n, player in enumerate(("203.0.113.5", "203.0.113.5", "203.0.113.5", "203.0.113.6")):
                reader, writer = await open_from(port, player)
                writers.append(writer)
                writer.write(wire.frame(wire.join_message(wire.group_id("secret1"), f"P{n}", f"P{n}")))
                joined.append(((await asyncio.wait_for(wire.read_frame(reader), 2))[0], player))
            self.assertEqual(joined, [(wire.WELCOME, "203.0.113.5")] * 2 + [(wire.ERROR, "203.0.113.5"),
                                                                             (wire.WELCOME, "203.0.113.6")])
            self.assertEqual(sorted(m.address for m in relay.members.values()),
                             ["203.0.113.5", "203.0.113.5", "203.0.113.6"])
            for writer in writers:
                writer.close()
        with mock.patch.object(wire, "MAX_PER_ADDRESS", 2):
            self.run_relay(test)

    def test_password(self) -> None:
        self.assertEqual(wire.parse_address("relay.example"), ("", "relay.example", wire.DEFAULT_PORT, "", ""))
        self.assertEqual(wire.parse_address(" pass@word@relay.example:1234 "),
                         ("pass@word", "relay.example", 1234, "", ""))
        self.assertEqual(wire.parse_address("pw@[::1]:5"), ("pw", "::1", 5, "", ""))
        self.assertEqual(wire.shown_address("pw@relay.example:1234"), "relay.example:1234")
        # WebSocket: the password before the scheme or in the URL; https:// is taken for wss://
        self.assertEqual(wire.parse_address("wss://ghosts.example"), ("", "ghosts.example", 443, "wss", "/"))
        self.assertEqual(wire.parse_address("pass@word@wss://ghosts.example/relay"),
                         ("pass@word", "ghosts.example", 443, "wss", "/relay"))
        self.assertEqual(wire.parse_address("wss://pw@ghosts.example:8443"), ("pw", "ghosts.example", 8443, "wss", "/"))
        self.assertEqual(wire.parse_address(" HTTPS://ghosts.example "), ("", "ghosts.example", 443, "wss", "/"))
        self.assertEqual(wire.parse_address("ws://127.0.0.1:43391"), ("", "127.0.0.1", 43391, "ws", "/"))
        self.assertEqual(wire.shown_address("pw@wss://ghosts.example/relay"), "wss://ghosts.example/relay")
        self.assertEqual(wire.shown_address("wss://pw@ghosts.example"), "wss://ghosts.example")
        with self.assertRaises(ValueError):
            wire.parse_address("ftp://ghosts.example")

        async def test(port: int, relay: wire.Relay) -> None:
            for password in (None, "", "wrong"):
                client = FakeClient(port)
                reply = await client.join("secret1", "A", password=password)
                self.assertEqual(reply[0], wire.ERROR)
                self.assertIn("Wrong" if password else "needs a password", reply[1:].decode())
                client.close()
            a = FakeClient(port)
            self.assertEqual((await a.join("secret1", "A", password="sesame"))[0], wire.WELCOME)
            self.assertEqual(len(relay.members), 1)
            a.close()
        with mock.patch.object(wire, "WRONG_PASSWORD_DELAY", 0):
            self.run_relay(test, password="sesame")

        async def open_relay(port: int, relay: wire.Relay) -> None:
            # without a password on the relay, a join with or without one is let in
            a, b = FakeClient(port), FakeClient(port)
            self.assertEqual((await a.join("secret1", "A", password=None))[0], wire.WELCOME)
            self.assertEqual((await b.join("secret1", "B", password="any"))[0], wire.WELCOME)
            a.close()
            b.close()
        self.run_relay(open_relay)

    def test_frames_are_batched(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            a, b = FakeClient(port), FakeClient(port)
            await a.join("secret1", "A")
            await b.join("secret1", "B")
            a.pos(SHOP, 1, 1)
            b.pos(SHOP, 1, 1)
            await a.collect()
            await b.collect()
            # frames sent within one batch reach the receiver in one write
            for x in range(5):
                a.pos(SHOP, x, 1)
            await asyncio.sleep(wire.BATCH * 3)
            data = await asyncio.wait_for(b.reader.read(4096), 2)
            self.assertEqual(len(data), 5 * (2 + 3 + wire.POS_BODY.size))
            a.close()
            b.close()
        self.run_relay(test)

    def test_nearest_are_shown(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            viewer, near, middle, far = (FakeClient(port) for _ in range(4))
            for client, slot in ((viewer, "V"), (near, "N"), (middle, "M"), (far, "F")):
                await client.join("secret1", slot)
            viewer.pos(SHOP, 0, 0)
            await asyncio.sleep(0.05)
            far.pos(SHOP, 9, 0)
            middle.pos(SHOP, 5, 0)
            await asyncio.sleep(0.05)
            near.pos(SHOP, 1, 0)  # the viewer is shown 2 players already: the third waits for choose()
            got = frames(await viewer.collect())
            self.assertEqual({p for p, _, _ in got}, {far.id, middle.id})
            await asyncio.sleep(0.05)
            relay.choose()
            got = await viewer.collect()
            self.assertEqual(offs(got), [(far.id, wire.OFF_AREA)])
            self.assertEqual(frames(got), [(near.id, 1, 0)])
            # a shown player stays unless another is KEEP_MARGIN nearer
            far.pos(SHOP, 4.5, 0)
            await asyncio.sleep(0.05)
            relay.choose()
            self.assertEqual(offs(await viewer.collect()), [])
            far.pos(SHOP, 2, 0)
            await asyncio.sleep(0.05)
            relay.choose()
            self.assertEqual(offs(await viewer.collect()), [(middle.id, wire.OFF_AREA)])
            for client in (viewer, near, middle, far):
                client.close()
        shown = wire.MAX_SHOWN
        wire.MAX_SHOWN = 2
        try:
            self.run_relay(test)
        finally:
            wire.MAX_SHOWN = shown

    def test_silent_member_is_hidden(self) -> None:
        async def test(port: int, relay: wire.Relay) -> None:
            a, b = FakeClient(port), FakeClient(port)
            await a.join("secret1", "A")
            await b.join("secret1", "B")
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

        self.assertIsNone(pos_body("POS none".split()))
        self.assertIsNone(pos_body("POS NoSuchLevel 1 2 3 0 0 0 0 0 0 207864 0 0 0 0".split()))
        self.assertIsNone(pos_body("POS Field 1 2 3 0 0 0 0 0 0 207864 0 0 0".split()))  # a field missing

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
                # A by WebSocket, B by plain TCP
                link.configure(True, ("ws://" if name == "A" else "") + address, "secret1", name, name)
                tasks.append(asyncio.create_task(link.run(exit_event)))

            async def until(test, seconds: float = 3) -> None:
                for _ in range(int(seconds / 0.02)):
                    if test():
                        return
                    await asyncio.sleep(0.02)
                self.fail(f"timed out; lines: {lines}")

            await until(lambda: all("GHOSTS 1" in lines[n] for n in lines) and len(links["A"].players) == 1)
            # the game's time goes along, 16 bits of it
            links["A"].on_pos("POS Field 38.250 10.5 128.25 90.0 0 0 0 1 70000 207864 0 0 0 0".split())
            links["B"].on_pos("POS Field 50.000 10.5 128.25 270.0 0 0 0 0 5 207864 0 0 0 0".split())
            await until(lambda: any(l.startswith("GHOST 0 ") for l in lines["B"]))
            self.assertIn("GHOST 0 Field 38.250 10.500 128.250 90.0 0 0 0 1 4464 207864 0 0 0 0 A", lines["B"])
            await until(lambda: "GHOST 0 Field 50.000 10.500 128.250 270.0 0 0 0 0 5 207864 0 0 0 0 B" in lines["A"])
            # rate: a burst of positions goes out at most every MIN_INTERVAL, the last one included
            for i in range(10):
                links["A"].on_pos(f"POS Field {39 + i} 10.5 128.25 90 0 0 0 0 {100 + i} 207864 0 0 0 0".split())
            await until(lambda: "GHOST 0 Field 48.000 10.500 128.250 90.0 0 0 0 0 109 207864 0 0 0 0 A" in lines["B"])
            self.assertLess(sum(l.startswith("GHOST 0 Field 4") for l in lines["B"]), 5)
            # what the player does is not held back by the rate: another animation, a turn, stopping
            links["A"].on_pos("POS Field 48 10.5 128.25 90 0 0 0 0 110 207864 0 0 0 0".split())
            links["A"].on_pos("POS Field 48 10.5 128.25 90 7 0 0 0 111 207864 0 0 0 0".split())
            links["A"].on_pos("POS Field 48 10.5 128.25 90 7 40 0 0 112 207864 0 0 0 0".split())
            links["A"].on_pos("POS Field 48 10.5 128.25 135 7 50 0 0 113 207864 0 0 0 0".split())
            links["A"].on_pos("POS Field 48 10.5 128.25 135 7 60 0 4 114 207864 0 0 0 0".split())
            for time_ms in (111, 113, 114):
                await until(lambda: any(l.startswith("GHOST 0 Field 48.000 ") and f" {time_ms} 207864 0 0 0 0 A" in l
                                        for l in lines["B"]))
            self.assertIn("GHOST 0 Field 48.000 10.500 128.250 135.0 7 60 0 4 114 207864 0 0 0 0 A", lines["B"])
            # nor is a change of tunic (red), face (eyes closed, then smiling and the ears down) or of the animation on
            # the upper body (idle_carry)
            links["A"].on_pos("POS Field 48 10.5 128.25 135 7 60 0 4 115 207864 1 0 0 0".split())
            await until(lambda: "GHOST 0 Field 48.000 10.500 128.250 135.0 7 60 0 4 115 207864 1 0 0 0 A" in lines["B"])
            links["A"].on_pos("POS Field 48 10.5 128.25 135 7 60 0 4 116 207864 1 1 0 0".split())
            links["A"].on_pos("POS Field 48 10.5 128.25 135 7 60 0 4 117 207864 1 3 3 1".split())
            links["A"].on_pos("POS Field 48 10.5 128.25 135 7 60 38 4 118 207864 1 3 3 1".split())
            for line in ("135.0 7 60 0 4 116 207864 1 1 0 0", "135.0 7 60 0 4 117 207864 1 3 3 1",
                         "135.0 7 60 38 4 118 207864 1 3 3 1"):
                await until(lambda: f"GHOST 0 Field 48.000 10.500 128.250 {line} A" in lines["B"])
            # A goes inside: B's ghost of A turns off
            links["A"].on_pos("POS Shop 1 0 1 0 0 0 0 0 0 207864 0 0 0 0".split())
            await until(lambda: "GHOST 0 off" in lines["B"])
            # ghosts turned off: A's link leaves the group and tells the game
            links["A"].configure(False, address, "secret1", "A", "A")
            await until(lambda: links["A"].status.text == "Ghosts are off." and not links["B"].players)
            self.assertEqual(lines["A"][-2:], ["GHOST 0 off", "GHOSTS 0"])
            exit_event.set()
            for link in links.values():
                link.wake.set()
            await asyncio.gather(*tasks)
            server.close()
        asyncio.run(main())

    def test_same_slot_name_of_another_multiworld(self) -> None:
        """On a relay with --nogroups another multiworld's player may have this one's slot name: it shows as a ghost,
        and this player itself never does."""
        from ..ghosts import GhostLink

        async def main() -> None:
            relay = wire.Relay(nogroups=True)
            server = await asyncio.start_server(relay.handle, "127.0.0.1", 0)
            address = f"127.0.0.1:{server.sockets[0].getsockname()[1]}"
            lines = {"one": [], "two": []}
            links = {name: GhostLink(lines[name].append) for name in lines}
            exit_event = asyncio.Event()
            tasks = []
            for name, link in links.items():
                link.on_game_connected()
                link.configure(True, address, "secret-" + name, "Link", "Link")
                tasks.append(asyncio.create_task(link.run(exit_event)))

            async def until(test, seconds: float = 3) -> None:
                for _ in range(int(seconds / 0.02)):
                    if test():
                        return
                    await asyncio.sleep(0.02)
                self.fail(f"timed out; lines: {lines}")

            one, two = links["one"], links["two"]
            await until(lambda: len(one.players) == 1 and len(two.players) == 1)
            self.assertEqual((list(one.players), list(two.players)), ([two.own_id], [one.own_id]))
            self.assertEqual(one.status.text, "Ghosts: connected to the relay, 1 other player online.")
            one.on_pos("POS Shop 1 0 1 0 0 0 0 0 0 207864 0 0 0 0".split())
            two.on_pos("POS Shop 2 0 2 0 0 0 0 0 0 207864 0 0 0 0".split())
            await until(lambda: one.ghosts[0] is not None and two.ghosts[0] is not None)
            self.assertIn("GHOST 0 Shop 2.000 0.000 2.000 0.0 0 0 0 0 0 207864 0 0 0 0 Link", lines["one"])
            self.assertIn("GHOST 0 Shop 1.000 0.000 1.000 0.0 0 0 0 0 0 207864 0 0 0 0 Link", lines["two"])
            # a frame of its own id (no relay sends one) is no ghost either
            one.handle(bytes([wire.PLAYER]) + struct.pack(">H", one.own_id) + wire.name_bytes("Link") * 2)
            one.handle(bytes([wire.FRAME]) + struct.pack(">H", one.own_id) +
                       wire.Pos(0, SHOP, 5, 0, 5, 0, 0, 0, 0, 0, wire.SHAPES_DEFAULT, 0, 0, 0, 0).pack())
            self.assertEqual((list(one.players), [g for g in one.ghosts if g is not None]), ([two.own_id],) * 2)
            self.assertFalse(any(l.startswith("GHOST") and " 5.000 " in l for l in lines["one"]))
            exit_event.set()
            for link in links.values():
                link.wake.set()
            await asyncio.gather(*tasks)
            server.close()
        asyncio.run(main())

    def test_link_sends_the_relay_password(self) -> None:
        from .. import checklist
        from ..ghosts import GhostLink

        async def main() -> None:
            relay = wire.Relay(password="sesame")
            server = await asyncio.start_server(relay.handle, "127.0.0.1", 0)
            address = f"127.0.0.1:{server.sockets[0].getsockname()[1]}"
            link = GhostLink(lambda line: None)
            exit_event = asyncio.Event()
            task = asyncio.create_task(link.run(exit_event))

            async def until(test) -> None:
                for _ in range(150):
                    if test():
                        return
                    await asyncio.sleep(0.02)
                self.fail(f"timed out; status: {link.status.text}")

            link.configure(True, "guess@" + address, "secret1", "A", "A")
            await until(lambda: link.status.level == checklist.ERROR)
            self.assertIn("Wrong relay password", link.status.text)
            link.configure(True, "sesame@" + address, "secret1", "A", "A")
            await until(lambda: link.joined)
            self.assertNotIn("sesame", link.status.text)
            exit_event.set()
            link.wake.set()
            await task
            server.close()
        with mock.patch.object(wire, "WRONG_PASSWORD_DELAY", 0):
            asyncio.run(main())


class TestGhostSecret(unittest.TestCase):
    def test_name_tags_switch(self) -> None:
        from ..ghosts import GhostLink

        lines: List[str] = []
        link = GhostLink(lines.append)
        link.set_names(False)
        self.assertEqual(lines, [])  # no game yet: it gets the switch when it connects
        link.on_game_connected()
        self.assertEqual(lines, ["GHOSTS 0", "GHOSTNAMES 0"])
        link.set_names(True)
        link.set_names(True)
        self.assertEqual(lines[2:], ["GHOSTNAMES 1"])

    def test_one_secret_per_multiworld(self) -> None:
        from test.general import setup_multiworld
        from worlds.AutoWorld import AutoWorldRegister
        from ..items import GAME
        world_type = AutoWorldRegister.world_types[GAME]
        one = setup_multiworld([world_type, world_type], ())
        other = setup_multiworld([world_type], ())
        secret = one.worlds[1].ghost_secret()
        self.assertRegex(secret, r"^[0-9a-f]{32}$")
        self.assertEqual(one.worlds[2].ghost_secret(), secret)
        self.assertNotEqual(other.worlds[1].ghost_secret(), secret)

    def test_client_takes_it_from_the_patch_file(self) -> None:
        import os
        import tempfile
        from ..client import LASContext
        from ..container import LASContainer

        async def main() -> None:
            with tempfile.TemporaryDirectory() as folder:
                path = os.path.join(folder, "AP_1_P1_Link.aplas")
                LASContainer(path, {"ghost_secret": "abc123"}, 1, "Link").write()
                ctx = LASContext(None, None, "127.0.0.1", 0, path)
                ctx.ghost_relay_arg = "127.0.0.1:1"
                ctx.update_ghosts()
                self.assertEqual((ctx.ghosts.secret, ctx.ghosts.slot, ctx.ghosts.address),
                                 ("abc123", "Link", "127.0.0.1:1"))
                ctx.ghost_relay_arg = ""
                ctx.update_ghosts()
                self.assertEqual(ctx.ghosts.waiting_for(), "Ghosts are off: enter a ghost relay to turn them on.")
        asyncio.run(main())

    def test_relay_from_the_patch_file_unless_set(self) -> None:
        import os
        import tempfile
        from .. import LASWorld
        from ..client import LASContext
        from ..container import LASContainer

        async def main() -> None:
            with tempfile.TemporaryDirectory() as folder:
                path = os.path.join(folder, "AP_1_P1_Link.aplas")
                LASContainer(path, {"ghost_secret": "abc123", "ghost_relay": "relay.example:43390"}, 1, "Link").write()
                ctx = LASContext(None, None, "127.0.0.1", 0, path)
                ctx.update_ghosts()
                self.assertEqual(ctx.ghosts.address, "relay.example:43390")
                LASWorld.settings.ghost_relay = "own.example:1"
                ctx.update_ghosts()
                self.assertEqual(ctx.ghosts.address, "own.example:1")
        saved = LASWorld.settings.ghost_relay
        try:
            LASWorld.settings.ghost_relay = ""
            asyncio.run(main())
        finally:
            LASWorld.settings.ghost_relay = saved


class TestPortForward(unittest.TestCase):
    def test_upnp_maps_and_removes_the_port(self) -> None:
        import http.server
        import threading
        service = "urn:schemas-upnp-org:service:WANIPConnection:1"
        actions: List[str] = []

        class Router(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args) -> None:
                pass

            def reply(self, status: int, body: str) -> None:
                data = body.encode()
                self.send_response(status)
                self.send_header("Content-Type", "text/xml")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self) -> None:
                self.reply(200, '<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><device>'
                                '<serviceList><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1'
                                '</serviceType><controlURL>/l3f</controlURL></service></serviceList><deviceList>'
                                f'<device><serviceList><service><serviceType>{service}</serviceType>'
                                '<controlURL>/ctl/IPConn</controlURL></service></serviceList></device></deviceList>'
                                '</device></root>')

            def do_POST(self) -> None:
                body = self.rfile.read(int(self.headers["Content-Length"])).decode()
                action = self.headers["SOAPAction"].strip('"').partition("#")[2]
                actions.append(f"{self.path} {action}" + (" lease=" + body.partition("<NewLeaseDuration>")[2]
                                                          .partition("<")[0] if "Lease" in body else ""))
                if action == "AddPortMapping" and "<NewLeaseDuration>0<" not in body:
                    # only permanent leases
                    self.reply(500, '<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/'
                                    'envelope/"><s:Body><s:Fault><detail><UPnPError><errorCode>725</errorCode>'
                                    '</UPnPError></detail></s:Fault></s:Body></s:Envelope>')
                    return
                answer = "<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>" \
                    if action == "GetExternalIPAddress" else ""
                self.reply(200, '<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/'
                                f'envelope/"><s:Body><u:{action}Response xmlns:u="{service}">{answer}'
                                f'</u:{action}Response></s:Body></s:Envelope>')

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Router)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            upnp = wire.UPnP(43390, f"http://127.0.0.1:{server.server_address[1]}/desc.xml")
            self.assertEqual(upnp.open(), "203.0.113.7")
            self.assertEqual(upnp.internal_ip, "127.0.0.1")
            upnp.close()
        finally:
            server.shutdown()
            server.server_close()
        self.assertEqual(actions, ["/ctl/IPConn AddPortMapping lease=3600", "/ctl/IPConn AddPortMapping lease=0",
                                   "/ctl/IPConn GetExternalIPAddress", "/ctl/IPConn DeletePortMapping"])
        self.assertTrue(wire.is_public("203.0.113.7") is False)  # documentation range
        self.assertTrue(wire.is_public("8.8.8.8"))
        self.assertFalse(wire.is_public("100.70.1.2"))  # carrier-grade NAT

    def test_unmapper_removes_the_mapping_once(self) -> None:
        """Closing the console window, logging off or shutting down removes the router's mapping; Ctrl+C is left to
        Python, and the relay's own stop afterwards removes nothing twice."""
        import logging

        class Mapping:
            name = "UPnP"
            closed = 0

            def close(self) -> None:
                self.closed += 1

        for event in wire.Unmapper.CLOSE_EVENTS:
            mapping = Mapping()
            unmapper = wire.Unmapper(mapping, logging.getLogger("test"))
            self.assertFalse(unmapper.console_event(0))  # CTRL_C_EVENT
            self.assertEqual(mapping.closed, 0)
            self.assertTrue(unmapper.console_event(event))
            unmapper.remove()
            self.assertEqual(mapping.closed, 1)
        unmapper = wire.Unmapper(Mapping(), logging.getLogger("test"))
        unmapper.watch_console()
        unmapper.unwatch_console()

    def test_nat_pmp_maps_and_removes_the_port(self) -> None:
        import socket
        import threading
        gateway = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        gateway.bind(("127.0.0.1", 0))
        requests: List[bytes] = []

        def answer() -> None:
            while True:
                try:
                    data, sender = gateway.recvfrom(64)
                except OSError:
                    return
                requests.append(data)
                if data[1] == 0:
                    gateway.sendto(struct.pack(">BBHI4s", 0, 128, 0, 1, socket.inet_aton("198.51.100.9")), sender)
                else:
                    lifetime = struct.unpack_from(">I", data, 8)[0]
                    # the router gives another external port, and a longer lease
                    gateway.sendto(struct.pack(">BBHIHHI", 0, 130, 0, 1, 43390, 43391 if lifetime else 0,
                                               7200 if lifetime else 0), sender)

        threading.Thread(target=answer, daemon=True).start()
        try:
            mapping = wire.NatPmp(43390, "127.0.0.1")
            mapping.PORT = gateway.getsockname()[1]
            self.assertEqual(mapping.open(), "198.51.100.9")
            self.assertEqual((mapping.external_port, mapping.lease, mapping.internal_ip), (43391, 7200, "127.0.0.1"))
            mapping.close()
        finally:
            gateway.close()
        self.assertEqual([r.hex() for r in requests], ["0000", "0002" + "0000" + "a97e" + "a97e" + "00000e10",
                                                       "0002" + "0000" + "a97e" + "0000" + "00000000"])
