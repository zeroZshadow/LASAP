"""Ghost relay: passes players' positions between the LAS clients of one run, for multiplayer ghosts (DESIGN.md,
Ghosts). Also the wire protocol the client uses (ghosts.py). Standard library only, so the file runs on its own:

    python3 ghost_relay.py [--host 0.0.0.0] [--port 43390]

Plain TCP with TCP_NODELAY. Every message is a frame: a u16 length (big-endian), then the payload, whose first byte is
the message type. A client joins a group, SHA-256("lasghost" | secret), computed by the client from its multiworld's
ghost secret (in the patch file), so the relay never sees the secret. The relay forwards a player's position to the group's members in the same level, and in
the overworld (Field) only to those within the 3x3 screens around the sender; it sends an "off" for a player who
leaves a member's area or the group.
"""
import argparse
import asyncio
import hashlib
import logging
import math
import socket
import struct
import time
from typing import Dict, List, Optional, Set, Tuple

PROTOCOL_VERSION = 1
DEFAULT_PORT = 43390

# Client -> relay
JOIN = 0x01  # u8 version, 32 bytes group, slot name, player name (each a u8 length and UTF-8)
POS = 0x02  # POS_BODY
NOWHERE = 0x03  # the player is in no level (title, file menu, level load, game not connected)
PING = 0x04  # keep-alive; the relay answers PONG
# Relay -> client
WELCOME = 0x81  # u8 version, u16 the client's player id
PLAYER = 0x82  # u16 player id, slot name, player name: a member of the group (sent for each member on joining)
FRAME = 0x83  # u16 player id, POS_BODY
OFF = 0x84  # u16 player id, u8 reason (OFF_AREA, OFF_LEFT): stop showing that player
PONG = 0x85
ERROR = 0x86  # UTF-8 text; the relay closes the connection

OFF_AREA = 0  # left the receiver's area, or is in no level
OFF_LEFT = 1  # left the group

# time (the sender's clock in ms, 16 bits), level id, room, x, y, z, yaw (360 degrees = 65536), anim id, anim time
# (ms), flags
POS_BODY = struct.Struct(">HHBfffHHHB")
FLAG_WARPED = 1  # the position jumped (level load, respawn, warp): snap instead of moving there
FLAG_HIDDEN = 2  # Link is not drawn
FLAG_STILL = 4  # Link did not move since the previous frame

NO_LEVEL = 0xFFFF
NO_ROOM = 0xFF
MAX_FRAME = 256

# Level ids: the index in this list (the RomFS level folders). Append only: ids must not change.
LEVELS = (
    "AnglersTunnelExit", "AnimalVillageCave", "AnimalVillageEnter", "ArmosShrine", "ArmosShrineCave", "BananaHouse",
    "BearHouse", "BedRoom", "BoomerangCave", "CatfishsMawEnter", "ChristineHouse", "CraneGame", "CuccoHouse",
    "DanpeiHouse", "DogHouse", "DrWrightHouse", "DreamShrine", "EagleKeyCave", "EagleTowerExit", "Ending",
    "EventMarin01", "EventMarin02", "FaceShrineEnter", "FairyFountain01", "FairyFountain02", "FairyFountain03", "Field",
    "FishingBoat", "FishingPond", "FlyingCuccoCave", "GhostHouse", "GopongaCave", "HiddenZoraHouse", "KanaletCastle",
    "KanaletCastleEnter", "KanaletCastleSouthCave", "LanmolaCave", "Library", "Lv01TailCave", "Lv02BottleGrotto",
    "Lv03KeyCavern", "Lv04AnglersTunnel", "Lv05CatfishsMaw", "Lv06FaceShrine", "Lv07EagleTower", "Lv08TurtleRock",
    "Lv09WindFishsEgg", "Lv10ClothesDungeon", "MadBattersWell01", "MadBattersWell02", "MadBattersWell03",
    "MadBattersWellEnter", "MadamHouse", "MagicPowderHouse", "MamboCave", "MamuCave", "MarinTarinHouse",
    "MarthaUkuku", "MermaidStatue", "MoriblinCave", "MysteriousWoodsCave01", "MysteriousWoodsCave02", "PanelEdit",
    "PanelLv01TailCave", "PanelLv02BottleGrotto", "PanelLv03KeyCavern", "PanelLv04AnglersTunnel",
    "PanelLv05CatfishsMaw", "PanelLv06FaceShrine", "PanelLv07EagleTower", "PanelLv08TurtleRock",
    "PanelLv10ClothesDungeon", "PanelLv11PanelOnlyDungeon", "QuadupletHouse", "RabbitBigHouse", "RapidsRideExit",
    "RapidsRideTicket", "RichardCave", "RichardHouse", "SchuleDonavitchHouse", "SeashellHouse", "Shop",
    "Tamaranch01", "Tamaranch02", "Tamaranch03", "Tamaranch04", "Tamaranch05", "TelephoneBox01_Ukuku1",
    "TelephoneBox02_Mebe", "TelephoneBox03_Kanalet", "TelephoneBox04_AnimalVillage", "TelephoneBox05_TurtleRock",
    "TelephoneBox06_Goponga", "TelephoneBox07_Ukuku2", "TelephoneBox08_Martha", "TownWell", "TracyHouse",
    "TurtleRockEnter", "UkukuCave01", "UkukuCave02", "UlriraHouse", "UnderGrave",
)
LEVEL_IDS = {name: i for i, name in enumerate(LEVELS)}
FIELD = LEVEL_IDS["Field"]

# Limits of the public relay
MAX_GROUP = 64
MAX_CLIENTS = 4000
MAX_PER_ADDRESS = 16
MAX_RATE = 40.0  # frames per second a client may send; more are dropped
SILENT_OFF = 10.0  # a member who sent no position for this long is shown to nobody
SILENT_DROP = 60.0  # a connection silent for this long is closed
MAX_BACKLOG = 32 * 1024  # bytes waiting to go to a receiver; frames beyond it are dropped


def group_id(secret: str) -> bytes:
    return hashlib.sha256(b"lasghost|" + secret.encode()).digest()


def field_screen(x: float, z: float) -> Tuple[int, int]:
    """The overworld screen (column, row) around a position: Field_<row><col> with col A = 0 and rows from 1."""
    col = min(15, max(0, int(x // 15)))
    row = min(16, max(1, int(z // 12) + 1))
    return col, row


def room_index(level: str, x: float, z: float) -> int:
    """The room byte of a position: in Field (row - 1) * 16 + column, elsewhere NO_ROOM."""
    if level != "Field":
        return NO_ROOM
    col, row = field_screen(x, z)
    return (row - 1) * 16 + col


def frame(payload: bytes) -> bytes:
    return struct.pack(">H", len(payload)) + payload


def name_bytes(text: str) -> bytes:
    data = text.encode("utf-8")[:64]
    return bytes([len(data)]) + data


def read_name(data: bytes, at: int) -> Tuple[str, int]:
    n = data[at]
    if at + 1 + n > len(data):
        raise ValueError("name past the end")
    return data[at + 1:at + 1 + n].decode("utf-8", "replace"), at + 1 + n


def join_message(group: bytes, slot: str, player: str) -> bytes:
    return bytes([JOIN, PROTOCOL_VERSION]) + group + name_bytes(slot) + name_bytes(player)


def parse_join(payload: bytes) -> Tuple[int, bytes, str, str]:
    version, group = payload[1], payload[2:34]
    if len(group) != 32:
        raise ValueError("short join")
    slot, at = read_name(payload, 34)
    player, _ = read_name(payload, at)
    return version, group, slot, player


async def read_frame(reader: asyncio.StreamReader) -> bytes:
    """The next frame's payload; raises IncompleteReadError at the end and ValueError on a bad length."""
    size = struct.unpack(">H", await reader.readexactly(2))[0]
    if size == 0 or size > MAX_FRAME:
        raise ValueError(f"frame of {size} bytes")
    return await reader.readexactly(size)


class Member:
    def __init__(self, relay: "Relay", writer: asyncio.StreamWriter, address: str):
        self.relay = relay
        self.writer = writer
        self.address = address
        self.id = -1
        self.group: Optional[bytes] = None
        self.slot = ""
        self.player = ""
        self.level = NO_LEVEL
        self.screen: Tuple[int, int] = (0, 0)
        self.body: Optional[bytes] = None  # the last POS_BODY, while in a level
        self.last_pos = 0.0  # when the last POS or NOWHERE arrived
        self.last_heard = time.monotonic()
        self.visible: Set["Member"] = set()  # members whose frames this one gets
        self.tokens = MAX_RATE
        self.token_time = time.monotonic()
        self.closed = False

    def send(self, payload: bytes, droppable: bool = False) -> None:
        if self.closed:
            return
        if droppable and self.writer.transport.get_write_buffer_size() > MAX_BACKLOG:
            return
        self.writer.write(frame(payload))

    def present(self) -> bool:
        return self.body is not None and self.level != NO_LEVEL

    def sees(self, other: "Member") -> bool:
        """Whether two members are in each other's area: the same level and, in Field, within one screen."""
        if not (self.present() and other.present()) or self.level != other.level:
            return False
        if self.level != FIELD:
            return True
        return abs(self.screen[0] - other.screen[0]) <= 1 and abs(self.screen[1] - other.screen[1]) <= 1

    def allow(self) -> bool:
        now = time.monotonic()
        self.tokens = min(MAX_RATE * 2, self.tokens + (now - self.token_time) * MAX_RATE)
        self.token_time = now
        if self.tokens < 1:
            return False
        self.tokens -= 1
        return True


class Relay:
    def __init__(self):
        self.groups: Dict[bytes, List[Member]] = {}
        self.members: Dict[int, Member] = {}
        self.connections: Set[Member] = set()
        self.next_id = 1
        self.log = logging.getLogger("ghost_relay")

    def new_id(self) -> int:
        for _ in range(0x10000):
            candidate = self.next_id
            self.next_id = self.next_id % 0xFFFE + 1
            if candidate not in self.members:
                return candidate
        raise RuntimeError("no free player id")

    async def handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        sock = writer.get_extra_info("socket")
        if sock is not None:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        peer = writer.get_extra_info("peername")
        address = peer[0] if peer else "?"
        member = Member(self, writer, address)
        try:
            if len(self.connections) >= MAX_CLIENTS:
                return self.refuse(member, "The ghost relay is full.")
            if sum(1 for m in self.connections if m.address == address) >= MAX_PER_ADDRESS:
                return self.refuse(member, "Too many connections from this address.")
            self.connections.add(member)
            payload = await asyncio.wait_for(read_frame(reader), 10)
            if payload[0] != JOIN:
                return self.refuse(member, "Join first.")
            version, group, slot, player = parse_join(payload)
            if version != PROTOCOL_VERSION:
                return self.refuse(member, f"The ghost relay speaks version {PROTOCOL_VERSION}, the client {version}. "
                                           f"Update the Link's Awakening world.")
            if len(self.groups.get(group, ())) >= MAX_GROUP and not any(
                    m.slot == slot for m in self.groups.get(group, ())):
                return self.refuse(member, "This group of players is full.")
            self.join(member, group, slot, player)
            while True:
                payload = await read_frame(reader)
                member.last_heard = time.monotonic()
                self.receive(member, payload)
                await writer.drain()
        except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError, OSError, ValueError, IndexError,
                struct.error):
            pass
        finally:
            self.leave(member)
            self.connections.discard(member)
            writer.close()

    def refuse(self, member: Member, text: str) -> None:
        member.send(bytes([ERROR]) + text.encode())

    def join(self, member: Member, group: bytes, slot: str, player: str) -> None:
        members = self.groups.setdefault(group, [])
        for old in [m for m in members if m.slot == slot]:
            # the same slot again (a reconnect before the old connection timed out): the new connection wins
            self.leave(old)
            old.closed = True
            old.writer.close()
        member.id = self.new_id()
        member.group, member.slot, member.player = group, slot, player
        self.members[member.id] = member
        member.send(bytes([WELCOME, PROTOCOL_VERSION]) + struct.pack(">H", member.id))
        announce = bytes([PLAYER]) + struct.pack(">H", member.id) + name_bytes(slot) + name_bytes(player)
        for other in members:
            member.send(bytes([PLAYER]) + struct.pack(">H", other.id) + name_bytes(other.slot) +
                        name_bytes(other.player))
            other.send(announce)
        members.append(member)
        self.log.info(f"join: player {member.id}, {len(members)} in the group, {len(self.members)} on the relay")

    def leave(self, member: Member) -> None:
        if member.group is None:
            return
        members = self.groups.get(member.group, [])
        if member in members:
            members.remove(member)
        if not members:
            self.groups.pop(member.group, None)
        self.members.pop(member.id, None)
        off = bytes([OFF]) + struct.pack(">HB", member.id, OFF_LEFT)
        for other in members:
            other.visible.discard(member)
            other.send(off)
        member.group = None
        self.log.info(f"leave: player {member.id}, {len(self.members)} on the relay")

    def receive(self, member: Member, payload: bytes) -> None:
        kind = payload[0]
        if kind == PING:
            member.send(bytes([PONG]))
        elif kind == POS and member.group is not None:
            if len(payload) != 1 + POS_BODY.size or not member.allow():
                return
            body = payload[1:]
            _, level, _, x, y, z, _, _, _, _ = POS_BODY.unpack(body)
            if not all(map(math.isfinite, (x, y, z))):
                return
            member.level = level  # ids past LEVELS (a newer client's) still compare
            member.screen = field_screen(x, z)
            member.body = body
            member.last_pos = time.monotonic()
            self.update(member, True)
        elif kind == NOWHERE and member.group is not None:
            member.body = None
            member.level = NO_LEVEL
            member.last_pos = time.monotonic()
            self.update(member, False)

    def update(self, member: Member, moved: bool) -> None:
        """After a member's position changed: forwards it to the members in its area, and fixes up who sees whom."""
        own = bytes([FRAME]) + struct.pack(">H", member.id) + member.body if member.body is not None else b""
        for other in self.groups.get(member.group or b"", ()):
            if other is member:
                continue
            if member.sees(other):
                if moved:
                    other.send(own, droppable=True)
                    other.visible.add(member)
                if other not in member.visible and other.body is not None:
                    # entered the area of a member who stands still: show it now, not at its next keep-alive
                    member.send(bytes([FRAME]) + struct.pack(">H", other.id) + other.body, droppable=True)
                    member.visible.add(other)
            else:
                self.hide(member, other)
                self.hide(other, member)

    @staticmethod
    def hide(viewer: Member, shown: Member) -> None:
        if shown in viewer.visible:
            viewer.visible.discard(shown)
            viewer.send(bytes([OFF]) + struct.pack(">HB", shown.id, OFF_AREA))

    async def sweep(self) -> None:
        """Once a second: members silent for SILENT_OFF are shown to nobody, connections silent for SILENT_DROP
        close."""
        while True:
            await asyncio.sleep(1)
            now = time.monotonic()
            for member in list(self.members.values()):
                if now - member.last_heard > SILENT_DROP:
                    member.writer.close()
                elif member.body is not None and now - member.last_pos > SILENT_OFF:
                    member.body = None
                    member.level = NO_LEVEL
                    self.update(member, False)


async def serve(host: str, port: int) -> None:
    relay = Relay()
    server = await asyncio.start_server(relay.handle, host, port)
    sweep = asyncio.create_task(relay.sweep())
    relay.log.info("listening on " + ", ".join(str(s.getsockname()) for s in server.sockets))
    try:
        async with server:
            await server.serve_forever()
    finally:
        sweep.cancel()


def main() -> None:
    parser = argparse.ArgumentParser(description="Ghost relay for Link's Awakening (Switch) Archipelago.")
    parser.add_argument("--host", default="0.0.0.0", help="Address to listen on (default: all).")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--quiet", action="store_true", help="Log errors only, not each join and leave.")
    args = parser.parse_args()
    logging.basicConfig(level=logging.WARNING if args.quiet else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    try:
        asyncio.run(serve(args.host, args.port))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
