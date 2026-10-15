"""Ghost relay: passes players' positions between the LAS clients of one run, for multiplayer ghosts (DESIGN.md,
Ghosts). Also the wire protocol the client uses (ghosts.py). Standard library only, so the file runs on its own:

    python3 ghost_relay.py [--host 0.0.0.0] [--port 43390] [--forward]

--forward has the router forward the port to this computer while the relay runs (NAT-PMP, else UPnP).

Plain TCP with TCP_NODELAY. Every message is a frame: a u16 length (big-endian), then the payload, whose first byte is
the message type. A client joins a group, SHA-256("lasghost" | secret), computed by the client from its multiworld's
ghost secret (in the patch file), so the relay never sees the secret. The relay forwards a player's position to the group's members in the same level, and in
the overworld (Field) only to those within the 3x3 screens around the sender, each member getting the nearest
MAX_SHOWN players of its area; it sends an "off" for a player who leaves a member's area or the group. A member's
frames go out together every BATCH seconds.
"""
import argparse
import asyncio
import hashlib
import logging
import math
import socket
import struct
import sys
import threading
import time
from typing import Dict, List, Optional, Set, Tuple, Union

PROTOCOL_VERSION = 2
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

ECHO_ID = 0xFFFF  # --echo: the player a member's own frames come back as (new_id never gives it)
OFF_AREA = 0  # left the receiver's area, or is in no level
OFF_LEFT = 1  # left the group

# time (the sender's clock in ms, 16 bits), level id, room, x, y, z, yaw (360 degrees = 65536), anim id (Link's
# PlayerAnimation.gsheet key + 1, 0 none), anim frame (tenths), flags, look
POS_BODY = struct.Struct(">HHBfffHHHBI")
FLAG_WARPED = 1  # the position jumped (level load, respawn, warp): snap instead of moving there
FLAG_HIDDEN = 2  # Link is not drawn
FLAG_STILL = 4  # Link did not move since the previous frame
FLAG_STARTED = 8  # an animation started since the previous frame sent (the same id again: it restarted)
# Look: bit i shows shape i of Link's model (20 shapes: his body and the items he holds, LOOK_SHAPES), bits 20-21 his
# tunic (0 green, 1 red, 2 blue)
LOOK_SHAPES = 20
LOOK_TUNIC_SHIFT = 20
LOOK_DEFAULT = 0x32bf8  # body, sword and shield, green tunic

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
BATCH = 0.02  # a receiver's frames go out together this often: one TCP segment instead of one per frame
MAX_SHOWN = 30  # players each member gets frames of at most: the nearest in its area (the game's ghost slots)
CHOOSE_EVERY = 0.5  # how often the nearest are chosen again
KEEP_MARGIN = 2.0  # a shown player stays unless another is this much nearer (units), so the choice does not flap


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
        self.visible: Set["Member"] = set()  # members whose frames this one gets (at most MAX_SHOWN)
        self.x = self.z = 0.0
        self.outbox: List[bytes] = []  # frames waiting for the next flush
        self.outbox_size = 0
        self.tokens = MAX_RATE
        self.token_time = time.monotonic()
        self.closed = False

    def send(self, payload: bytes, droppable: bool = False) -> None:
        if self.closed:
            return
        if droppable and self.writer.transport.get_write_buffer_size() + self.outbox_size > MAX_BACKLOG:
            return
        data = frame(payload)
        if not self.outbox:
            asyncio.get_running_loop().call_later(BATCH, self.flush)
        self.outbox.append(data)
        self.outbox_size += len(data)

    def flush(self) -> None:
        if self.outbox and not self.closed and not self.writer.is_closing():
            self.writer.write(b"".join(self.outbox))
        self.outbox.clear()
        self.outbox_size = 0

    def close(self) -> None:
        """Sends what waits, then closes the connection."""
        self.flush()
        self.closed = True
        self.writer.close()

    def distance(self, other: "Member") -> float:
        return math.hypot(self.x - other.x, self.z - other.z)

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
    def __init__(self, echo: Optional[float] = None):
        self.echo = echo  # for tests: each member also sees itself as ECHO_ID, this far along x
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
            member.close()

    def refuse(self, member: Member, text: str) -> None:
        member.send(bytes([ERROR]) + text.encode())

    def join(self, member: Member, group: bytes, slot: str, player: str) -> None:
        members = self.groups.setdefault(group, [])
        for old in [m for m in members if m.slot == slot]:
            # the same slot again (a reconnect before the old connection timed out): the new connection wins
            self.leave(old)
            old.close()
        member.id = self.new_id()
        member.group, member.slot, member.player = group, slot, player
        self.members[member.id] = member
        member.send(bytes([WELCOME, PROTOCOL_VERSION]) + struct.pack(">H", member.id))
        announce = bytes([PLAYER]) + struct.pack(">H", member.id) + name_bytes(slot) + name_bytes(player)
        for other in members:
            member.send(bytes([PLAYER]) + struct.pack(">H", other.id) + name_bytes(other.slot) +
                        name_bytes(other.player))
            other.send(announce)
        if self.echo is not None:
            member.send(bytes([PLAYER]) + struct.pack(">H", ECHO_ID) + name_bytes("Echo") + name_bytes("Echo"))
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
            _, level, _, x, y, z, _, _, _, _, _ = POS_BODY.unpack(body)
            if not all(map(math.isfinite, (x, y, z))):
                return
            member.level = level  # ids past LEVELS (a newer client's) still compare
            member.screen = field_screen(x, z)
            member.x, member.z = x, z
            member.body = body
            member.last_pos = time.monotonic()
            self.update(member, True)
            if self.echo is not None:
                fields = list(POS_BODY.unpack(body))
                fields[3] += self.echo
                member.send(bytes([FRAME]) + struct.pack(">H", ECHO_ID) + POS_BODY.pack(*fields), droppable=True)
        elif kind == NOWHERE and member.group is not None:
            member.body = None
            member.level = NO_LEVEL
            member.last_pos = time.monotonic()
            if self.echo is not None:
                member.send(bytes([OFF]) + struct.pack(">HB", ECHO_ID, OFF_AREA))
            self.update(member, False)

    def update(self, member: Member, moved: bool) -> None:
        """After a member's position changed: forwards it to the members in its area who are shown it, and fixes up
        who sees whom. A member with MAX_SHOWN shown players gets a new one when choose() finds it among the
        nearest."""
        own = bytes([FRAME]) + struct.pack(">H", member.id) + member.body if member.body is not None else b""
        for other in self.groups.get(member.group or b"", ()):
            if other is member:
                continue
            if member.sees(other):
                if moved and (member in other.visible or len(other.visible) < MAX_SHOWN):
                    other.send(own, droppable=True)
                    other.visible.add(member)
                if other not in member.visible and other.body is not None and len(member.visible) < MAX_SHOWN:
                    # entered the area of a member who stands still: show it now, not at its next keep-alive
                    self.show(member, other)
            else:
                self.hide(member, other)
                self.hide(other, member)

    @staticmethod
    def show(viewer: Member, shown: Member) -> None:
        viewer.send(bytes([FRAME]) + struct.pack(">H", shown.id) + shown.body, droppable=True)
        viewer.visible.add(shown)

    def choose(self) -> None:
        """Each member is shown the MAX_SHOWN nearest players in its area; one shown stays unless another is
        KEEP_MARGIN nearer."""
        for members in self.groups.values():
            for viewer in members:
                if viewer.body is None:
                    continue
                around = [m for m in members if m is not viewer and m.body is not None and viewer.sees(m)]
                if len(around) <= MAX_SHOWN and all(m in viewer.visible for m in around):
                    continue
                around.sort(key=lambda m: viewer.distance(m) - (KEEP_MARGIN if m in viewer.visible else 0))
                nearest = set(around[:MAX_SHOWN])
                for gone in [m for m in viewer.visible if m not in nearest]:
                    self.hide(viewer, gone)
                for new in [m for m in around[:MAX_SHOWN] if m not in viewer.visible]:
                    self.show(viewer, new)

    async def choosing(self) -> None:
        while True:
            await asyncio.sleep(CHOOSE_EVERY)
            self.choose()

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
                    member.close()
                elif member.body is not None and now - member.last_pos > SILENT_OFF:
                    member.body = None
                    member.level = NO_LEVEL
                    self.update(member, False)


class ForwardError(Exception):
    pass


class UPnP:
    """Forwards the relay's port on the router (UPnP Internet Gateway Device): finds the router by SSDP, then adds a
    TCP port mapping to this computer through its WANIPConnection or WANPPPConnection service, renewed while the relay
    runs and removed when it stops."""
    SEARCH_TARGETS = ("urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                      "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                      "urn:schemas-upnp-org:service:WANIPConnection:1",
                      "urn:schemas-upnp-org:service:WANPPPConnection:1")
    SERVICES = ("urn:schemas-upnp-org:service:WANIPConnection:", "urn:schemas-upnp-org:service:WANPPPConnection:")
    name = "UPnP"
    LEASE = 3600  # seconds; renewed every LEASE / 2
    DESCRIPTION = "LAS ghost relay"

    def __init__(self, port: int, location: Optional[str] = None):
        self.port = port
        self.external_port = port
        self.location = location  # the router's description URL; None: found by SSDP
        self.control_url = ""
        self.service = ""
        self.internal_ip = ""
        self.lease = self.LEASE

    @staticmethod
    def discover(timeout: float = 3.0) -> str:
        """The description URL of the first gateway that answers an SSDP search, sent on each of this computer's IPv4
        addresses (the default one may be a virtual or VPN adapter's)."""
        try:
            infos = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)
            addresses = sorted({str(info[4][0]) for info in infos})
        except OSError:
            addresses = []
        sockets = []
        try:
            for address in addresses or ["0.0.0.0"]:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
                sockets.append(sock)
                try:
                    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
                    if address != "0.0.0.0":
                        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(address))
                    sock.bind((address, 0))
                    sock.setblocking(False)
                    for target in UPnP.SEARCH_TARGETS:
                        request = (f"M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
                                   f"MX: 2\r\nST: {target}\r\n\r\n")
                        sock.sendto(request.encode(), ("239.255.255.250", 1900))
                except OSError:
                    continue
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                for sock in sockets:
                    try:
                        data, _ = sock.recvfrom(4096)
                    except OSError:
                        continue
                    for line in data.decode("latin1").split("\r\n"):
                        name, _, value = line.partition(":")
                        if name.strip().lower() == "location" and value.strip():
                            return value.strip()
                time.sleep(0.05)
        finally:
            for sock in sockets:
                sock.close()
        raise ForwardError("no router answered the UPnP search (is UPnP on in the router?)")

    def find_service(self, location: str) -> None:
        import urllib.parse
        import urllib.request
        import xml.etree.ElementTree as ElementTree
        with urllib.request.urlopen(location, timeout=5) as response:
            root = ElementTree.fromstring(response.read())
        base = location
        for element in root.iter():
            text = (element.text or "").strip()
            if element.tag.endswith("URLBase") and text:
                base = text
        for service in root.iter():
            if not service.tag.endswith("}service") and service.tag != "service":
                continue
            fields = {child.tag.rpartition("}")[2]: (child.text or "").strip() for child in service}
            if fields.get("serviceType", "").startswith(self.SERVICES) and fields.get("controlURL"):
                self.service = fields["serviceType"]
                self.control_url = urllib.parse.urljoin(base, fields["controlURL"])
                break
        else:
            raise ForwardError("the router offers no WAN connection service for port mappings")
        # this computer's address on the router's network
        host = urllib.parse.urlparse(self.control_url).hostname or ""
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.connect((host, 1900))
            self.internal_ip = probe.getsockname()[0]
        finally:
            probe.close()

    def call(self, action: str, **arguments: object) -> Dict[str, str]:
        import urllib.error
        import urllib.request
        import xml.etree.ElementTree as ElementTree
        from xml.sax.saxutils import escape
        body = "".join(f"<{name}>{escape(str(value))}</{name}>" for name, value in arguments.items())
        envelope = ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
                    's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>'
                    f'<u:{action} xmlns:u="{self.service}">{body}</u:{action}></s:Body></s:Envelope>')
        request = urllib.request.Request(self.control_url, envelope.encode(), method="POST", headers={
            "Content-Type": 'text/xml; charset="utf-8"', "SOAPAction": f'"{self.service}#{action}"'})
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                root = ElementTree.fromstring(response.read())
        except urllib.error.HTTPError as error:
            text = error.read().decode("utf-8", "replace")
            code = text.partition("<errorCode>")[2].partition("<")[0]
            raise ForwardError(f"{action} failed: UPnP error {code or error.code}") from None
        return {element.tag.rpartition("}")[2]: (element.text or "") for element in root.iter()}

    def add(self) -> None:
        mapping = dict(NewRemoteHost="", NewExternalPort=self.port, NewProtocol="TCP", NewInternalPort=self.port,
                       NewInternalClient=self.internal_ip, NewEnabled=1,
                       NewPortMappingDescription=self.DESCRIPTION)
        try:
            self.call("AddPortMapping", **mapping, NewLeaseDuration=self.lease)
        except ForwardError as error:
            if self.lease == 0 or "725" not in str(error):  # 725: only permanent leases
                raise
            self.lease = 0
            self.call("AddPortMapping", **mapping, NewLeaseDuration=0)

    def open(self) -> str:
        """Maps the port; returns the router's external address."""
        location = self.location or self.discover()
        self.location = location
        self.find_service(location)
        self.add()
        try:
            return self.call("GetExternalIPAddress").get("NewExternalIPAddress", "")
        except ForwardError:
            return ""

    def close(self) -> None:
        self.call("DeletePortMapping", NewRemoteHost="", NewExternalPort=self.port, NewProtocol="TCP")


def default_gateways() -> List[str]:
    """The default gateway's address: the routing table (Linux), `route` (Windows, macOS), else x.y.z.1 of each of
    this computer's IPv4 addresses."""
    import re
    import subprocess
    import sys
    found: List[str] = []
    try:
        if sys.platform.startswith("linux"):
            with open("/proc/net/route") as table:
                for line in table.read().splitlines()[1:]:
                    fields = line.split()
                    if len(fields) > 2 and fields[1] == "00000000":
                        found.append(socket.inet_ntoa(struct.pack("<I", int(fields[2], 16))))
        else:
            command = ["route", "print", "-4", "0.0.0.0"] if sys.platform == "win32" \
                else ["route", "-n", "get", "default"]
            output = subprocess.run(command, capture_output=True, text=True, timeout=5).stdout
            pattern = r"^\s*0\.0\.0\.0\s+0\.0\.0\.0\s+(\d+\.\d+\.\d+\.\d+)" if sys.platform == "win32" \
                else r"gateway:\s*(\d+\.\d+\.\d+\.\d+)"
            found += re.findall(pattern, output, re.MULTILINE)
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            found.append(str(info[4][0]).rpartition(".")[0] + ".1")
    except OSError:
        pass
    return list(dict.fromkeys(address for address in found if not address.startswith("127.")))


class NatPmp:
    """Forwards the relay's port on the router with NAT-PMP (RFC 6886, UDP port 5351 of the default gateway): the
    router maps a port to the computer that asks, for a lease renewed while the relay runs, and removes it on
    request. The router picks the external port; it is the requested one when free."""
    name = "NAT-PMP"
    LEASE = 3600
    PORT = 5351

    def __init__(self, port: int, gateway: Optional[str] = None):
        self.port = port
        self.external_port = port
        self.gateway = gateway  # None: the default gateway
        self.internal_ip = ""
        self.lease = self.LEASE

    def request(self, payload: bytes, size: int) -> bytes:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.connect((self.gateway or "", self.PORT))
            self.internal_ip = sock.getsockname()[0]
            wait = 0.25
            for _ in range(4):
                sock.send(payload)
                sock.settimeout(wait)
                try:
                    data = sock.recv(64)
                except (socket.timeout, ConnectionResetError):
                    wait *= 2
                    continue
                if len(data) >= size and data[1] == payload[1] + 128:
                    result = struct.unpack_from(">H", data, 2)[0]
                    if result != 0:
                        raise ForwardError(f"the router refused (NAT-PMP result {result})")
                    return data
        finally:
            sock.close()
        raise ForwardError("the router does not answer NAT-PMP")

    def add(self) -> None:
        data = self.request(struct.pack(">BBHHHI", 0, 2, 0, self.port, self.external_port, self.lease), 16)
        self.external_port, self.lease = struct.unpack_from(">HI", data, 10)

    def open(self) -> str:
        """Maps the port; returns the router's external address."""
        for gateway in [self.gateway] if self.gateway else default_gateways():
            self.gateway = gateway
            try:
                data = self.request(struct.pack(">BB", 0, 0), 12)
            except (ForwardError, OSError):
                continue
            self.add()
            return socket.inet_ntoa(data[8:12])
        raise ForwardError("no gateway answers NAT-PMP")

    def close(self) -> None:
        self.request(struct.pack(">BBHHHI", 0, 2, 0, self.port, 0, 0), 16)


class Unmapper:
    """Removes the router's port mapping once: when the relay stops, and on Windows also when its console window is
    closed, the user logs off or Windows shuts down. Python turns only Ctrl+C and Ctrl+Break into KeyboardInterrupt;
    after the other console events Windows ends the process once the handlers return (within about 5 s), so the
    handler removes the mapping itself, on the thread Windows calls it on."""
    CLOSE_EVENTS = (2, 5, 6)  # CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT, CTRL_SHUTDOWN_EVENT

    def __init__(self, mapping: Union["NatPmp", "UPnP"], log: logging.Logger):
        self.mapping, self.log = mapping, log
        self.lock = threading.Lock()
        self.done = False
        self.handler = None  # the console handler, kept referenced while installed

    def remove(self) -> None:
        with self.lock:
            if self.done:
                return
            self.done = True
        try:
            self.mapping.close()
            self.log.info(f"{self.mapping.name}: port mapping removed")
        except (ForwardError, OSError) as error:
            self.log.warning(f"{self.mapping.name}: removing the port mapping failed: {error}")

    def console_event(self, event: int) -> bool:
        """The console handler: removes the mapping on a close, logoff or shutdown; leaves Ctrl+C and Ctrl+Break to
        Python's handler."""
        if event in self.CLOSE_EVENTS:
            self.remove()
            return True
        return False

    def watch_console(self) -> None:
        if sys.platform != "win32":
            return
        import ctypes
        from ctypes import wintypes
        self.handler = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.DWORD)(self.console_event)
        ctypes.windll.kernel32.SetConsoleCtrlHandler(self.handler, True)

    def unwatch_console(self) -> None:
        if self.handler is not None:
            import ctypes
            ctypes.windll.kernel32.SetConsoleCtrlHandler(self.handler, False)
            self.handler = None


def forward_port(port: int) -> Tuple[Union["NatPmp", UPnP], str]:
    """Maps the port with NAT-PMP, else UPnP: the mapping (add, close, lease, external_port, internal_ip, name) and the
    router's external address."""
    errors = []
    for mapping in (NatPmp(port), UPnP(port)):
        try:
            return mapping, mapping.open()
        except (ForwardError, OSError) as error:
            errors.append(f"{mapping.name}: {error}")
    raise ForwardError("; ".join(errors))


def is_public(address: str) -> bool:
    import ipaddress
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return ip.is_global and ip not in ipaddress.ip_network("100.64.0.0/10")


async def serve(host: str, port: int, echo: Optional[float] = None, forward: bool = False) -> None:
    relay = Relay(echo)
    server = await asyncio.start_server(relay.handle, host, port)
    sweep = asyncio.create_task(relay.sweep())
    choosing = asyncio.create_task(relay.choosing())
    relay.log.info("listening on " + ", ".join(str(s.getsockname()) for s in server.sockets))
    renew = None
    unmapper = None
    if forward:
        try:
            mapping, external = await asyncio.to_thread(forward_port, port)
        except ForwardError as error:
            relay.log.error(f"Port forwarding: {error}. Forward TCP port {port} to this computer on the router "
                            "instead.")
        else:
            m = mapping  # NatPmp or UPnP
            unmapper = Unmapper(m, relay.log)
            unmapper.watch_console()
            relay.log.info(f"{m.name}: the router forwards TCP port {m.external_port} to {m.internal_ip}:{port}")
            if external and is_public(external):
                relay.log.info(f"{m.name}: players enter the relay {external}:{m.external_port}")
            else:
                relay.log.warning(f"{m.name}: the router's internet address is {external or 'unknown'}, not a public "
                                  "one (the provider may share it between customers): players outside may not "
                                  "reach the relay")

            async def keep() -> None:
                while m.lease:
                    await asyncio.sleep(m.lease / 2)
                    try:
                        await asyncio.to_thread(m.add)
                    except (ForwardError, OSError) as error:
                        relay.log.warning(f"{m.name}: renewing the port mapping failed: {error}")
            renew = asyncio.create_task(keep())
    try:
        async with server:
            await server.serve_forever()
    finally:
        sweep.cancel()
        choosing.cancel()
        if renew is not None:
            renew.cancel()
        if unmapper is not None:
            unmapper.remove()
            unmapper.unwatch_console()


def main() -> None:
    parser = argparse.ArgumentParser(description="Ghost relay for Link's Awakening (Switch) Archipelago.")
    parser.add_argument("--host", default="0.0.0.0", help="Address to listen on (default: all).")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--quiet", action="store_true", help="Log errors only, not each join and leave.")
    parser.add_argument("--echo", type=float, metavar="DX",
                        help="For tests: each player also sees its own frames as a player Echo, DX units along x.")
    parser.add_argument("--forward", action="store_true",
                        help="Have the router forward the port to this computer while the relay runs (NAT-PMP, "
                             "else UPnP).")
    args = parser.parse_args()
    logging.basicConfig(level=logging.WARNING if args.quiet else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    try:
        asyncio.run(serve(args.host, args.port, args.echo, args.forward))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
