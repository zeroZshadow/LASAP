"""Multiplayer ghosts, the client's part: the link to the ghost relay (ghost_relay.py). The game's POS lines go to the
relay as frames; frames of the players around this one come back as GHOST lines for the game (DESIGN.md, Ghosts)."""
import asyncio
import math
import socket
import struct
from typing import Callable, Dict, List, Optional, Tuple

from . import checklist
from . import ghost_relay as wire

# The community relay every player uses unless the client window names another (las_options.ghost_relay).
RELAY_ADDRESS = ""
GHOST_SLOTS = 4  # ghosts the game shows at once
MIN_INTERVAL = 0.045  # at most ~20 frames a second go to the relay; a new animation id goes at once
PING_INTERVAL = 10.0
SILENT_TIMEOUT = 30.0  # the relay answers pings; a link that hears nothing this long reconnects
RETRY_DELAYS = (2, 5, 10, 30, 60)


def parse_address(address: str) -> Tuple[str, int]:
    host, _, port = address.strip().rpartition(":")
    if not host:
        return address.strip(), wire.DEFAULT_PORT
    return host.strip("[]"), int(port)


def pos_body(parts: List[str], seq: int) -> Optional[bytes]:
    """A POS line's fields (`POS <level> <room> <x> <y> <z> <yaw> <anim> <anim_ms> [<flags>]`) as a frame body, or
    None when Link is in no level the relay knows (`POS none`, a level missing from wire.LEVELS)."""
    if len(parts) < 9 or parts[1] not in wire.LEVEL_IDS:
        return None
    try:
        room = int(parts[2])
        x, y, z, yaw = (float(p) for p in parts[3:7])
        anim, anim_ms = int(parts[7]), int(parts[8])
        flags = int(parts[9]) if len(parts) > 9 else 0
    except ValueError:
        return None
    if not all(map(math.isfinite, (x, y, z, yaw))):
        return None
    return wire.POS_BODY.pack(seq & 0xFFFF, wire.LEVEL_IDS[parts[1]], room & 0xFF, x, y, z,
                              round(yaw % 360 * 65536 / 360) & 0xFFFF, anim & 0xFFFF, anim_ms & 0xFFFF, flags & 0xFF)


def ghost_line(n: int, slot: str, body: bytes) -> Optional[str]:
    _, level, _, x, y, z, yaw, anim, anim_ms, flags = wire.POS_BODY.unpack(body)
    if level >= len(wire.LEVELS):
        return None
    name = " ".join(slot.replace("|", "/").split()) or "?"
    return (f"GHOST {n} {wire.LEVELS[level]} {x:.3f} {y:.3f} {z:.3f} {yaw * 360 / 65536:.1f} {anim} {anim_ms} {flags} "
            f"{name}")


class GhostLink:
    """Keeps the client joined to its run's group on the relay while ghosts are on, the address and the seed are
    known; `configure` reconnects when any of them changes."""

    def __init__(self, send_game: Callable[[str], None], changed: Callable[[], None] = lambda: None):
        self.send_game = send_game
        self.changed = changed  # the status changed (the Patch tab shows it)
        self.enabled = False
        self.address = ""
        self.password = ""
        self.seed_hash = ""
        self.slot = ""
        self.player = ""
        self.game_connected = False
        self.status = checklist.Check(checklist.INFO, "")
        self.writer: Optional[asyncio.StreamWriter] = None
        self.joined = False
        self.own_id = -1
        self.players: Dict[int, Tuple[str, str]] = {}  # the group's other members: id -> (slot, player name)
        self.ghosts: List[Optional[int]] = [None] * GHOST_SLOTS  # the player id each ghost slot shows
        self.seq = 0
        self.last_sent = 0.0
        self.pending: Optional[bytes] = None  # a POS body waiting for MIN_INTERVAL; b"" for NOWHERE
        self.sent_anim = -1  # the animation id of the last frame sent
        self.flush_handle: Optional[asyncio.TimerHandle] = None
        self.sent_frames = 0
        self.received_frames = 0
        self.wake = asyncio.Event()

    # settings and state

    def configure(self, enabled: bool, address: str, password: str, seed_hash: Optional[str], slot: str,
                  player: str) -> None:
        new = (enabled, address.strip(), password, seed_hash or "", slot, player or slot)
        if new == (self.enabled, self.address, self.password, self.seed_hash, self.slot, self.player):
            return
        self.enabled, self.address, self.password, self.seed_hash, self.slot, self.player = new
        self.drop()
        self.wake.set()

    def set_status(self, level: str, text: str) -> None:
        if (level, text) != (self.status.level, self.status.text):
            self.status = checklist.Check(level, text)
            self.changed()

    def waiting_for(self) -> str:
        """What keeps the link from joining, or ""."""
        if not self.enabled:
            return "Ghosts are off."
        if not self.address:
            return "Ghosts: no relay address (advanced settings)."
        if not self.seed_hash:
            return "Ghosts: waiting for the game or the server, to learn the seed."
        if not self.slot:
            return "Ghosts: waiting for the slot name (patch file or server)."
        return ""

    # the game's side

    def on_game_connected(self) -> None:
        self.game_connected = True
        self.send_game(f"GHOSTS {int(self.joined)}")
        for n, player_id in enumerate(self.ghosts):
            if player_id is not None:
                self.send_game(f"GHOST {n} off")
        self.ghosts = [None] * GHOST_SLOTS  # each shows again with its player's next frame

    def on_game_lost(self) -> None:
        self.game_connected = False
        self.queue(b"")

    def on_pos(self, parts: List[str]) -> None:
        """A POS line from the game."""
        body = pos_body(parts, self.seq)
        self.queue(body if body is not None else b"")

    def queue(self, body: bytes) -> None:
        self.pending = body
        if not self.joined:
            return
        if body and wire.POS_BODY.unpack(body)[7] != self.sent_anim:
            if self.flush_handle is not None:
                self.flush_handle.cancel()
            self.flush()
            return
        if self.flush_handle is not None:
            return
        loop = asyncio.get_running_loop()
        wait = self.last_sent + MIN_INTERVAL - loop.time()
        if wait > 0:
            self.flush_handle = loop.call_later(wait, self.flush)
        else:
            self.flush()

    def flush(self) -> None:
        self.flush_handle = None
        if self.pending is None or self.writer is None or not self.joined:
            return
        body, self.pending = self.pending, None
        if body:
            body = struct.pack(">H", self.seq & 0xFFFF) + body[2:]
            self.seq += 1
            self.sent_anim = wire.POS_BODY.unpack(body)[7]
            self.write(bytes([wire.POS]) + body)
            self.sent_frames += 1
        else:
            self.write(bytes([wire.NOWHERE]))
        self.last_sent = asyncio.get_running_loop().time()

    def write(self, payload: bytes) -> None:
        if self.writer is not None:
            self.writer.write(wire.frame(payload))

    # the relay's side

    def drop(self) -> None:
        """Closes the relay connection; the game stops showing ghosts."""
        if self.writer is not None:
            self.writer.close()
            self.writer = None
        self.left()

    def left(self) -> None:
        was_joined = self.joined
        self.joined = False
        self.players.clear()
        for n, player_id in enumerate(self.ghosts):
            if player_id is not None:
                self.send_game(f"GHOST {n} off")
        self.ghosts = [None] * GHOST_SLOTS
        if self.flush_handle is not None:
            self.flush_handle.cancel()
            self.flush_handle = None
        if was_joined and self.game_connected:
            self.send_game("GHOSTS 0")

    def handle(self, payload: bytes) -> None:
        kind = payload[0]
        if kind == wire.FRAME:
            player_id = struct.unpack_from(">H", payload, 1)[0]
            self.received_frames += 1
            self.show(player_id, payload[3:])
        elif kind == wire.OFF:
            player_id, reason = struct.unpack_from(">HB", payload, 1)
            self.hide(player_id)
            if reason == wire.OFF_LEFT:
                self.players.pop(player_id, None)
                self.joined_status()
        elif kind == wire.PLAYER:
            player_id = struct.unpack_from(">H", payload, 1)[0]
            slot, at = wire.read_name(payload, 3)
            player, _ = wire.read_name(payload, at)
            self.players[player_id] = (slot, player)
            self.joined_status()
        elif kind == wire.WELCOME:
            self.own_id = struct.unpack_from(">H", payload, 2)[0]
            self.joined = True
            self.joined_status()
            if self.game_connected:
                self.send_game("GHOSTS 1")
        elif kind == wire.ERROR:
            raise RelayRefused(payload[1:].decode("utf-8", "replace"))

    def joined_status(self) -> None:
        others = len(self.players)
        self.set_status(checklist.OK, f"Ghosts: connected to the relay, {others} other player"
                                      f"{'' if others == 1 else 's'} of this run online.")

    def show(self, player_id: int, body: bytes) -> None:
        if player_id not in self.players or self.players[player_id][0] == self.slot:
            return
        if player_id in self.ghosts:
            n = self.ghosts.index(player_id)
        elif None in self.ghosts:
            n = self.ghosts.index(None)
        else:
            return  # every ghost slot is taken; it shows once one frees
        line = ghost_line(n, self.players[player_id][0], body)
        if line is None:
            return
        self.ghosts[n] = player_id
        self.send_game(line)

    def hide(self, player_id: int) -> None:
        if player_id in self.ghosts:
            n = self.ghosts.index(player_id)
            self.ghosts[n] = None
            self.send_game(f"GHOST {n} off")

    async def run(self, exit_event: asyncio.Event) -> None:
        failures = 0
        while not exit_event.is_set():
            self.wake.clear()
            reason = self.waiting_for()
            if reason:
                self.set_status(checklist.INFO, reason)
                await self.wait(self.wake, None)
                continue
            try:
                await self.session()
                failures = 0
            except RelayRefused as e:
                self.set_status(checklist.ERROR, f"Ghosts: the relay refused: {e}")
                failures = len(RETRY_DELAYS) - 1
            except (OSError, asyncio.TimeoutError, asyncio.IncompleteReadError, ValueError, IndexError,
                    struct.error) as e:
                if not self.wake.is_set():
                    self.set_status(checklist.ERROR, f"Ghosts: cannot reach the relay {self.address} "
                                                     f"({e.__class__.__name__}{': ' + str(e) if str(e) else ''}).")
            finally:
                self.drop()
            if self.wake.is_set():
                continue  # the settings changed: connect again at once
            delay = RETRY_DELAYS[min(failures, len(RETRY_DELAYS) - 1)]
            failures += 1
            await self.wait(self.wake, delay)

    @staticmethod
    async def wait(event: asyncio.Event, seconds: Optional[float]) -> None:
        try:
            await asyncio.wait_for(event.wait(), seconds)
        except asyncio.TimeoutError:
            pass

    async def session(self) -> None:
        host, port = parse_address(self.address)
        self.set_status(checklist.INFO, f"Ghosts: connecting to the relay {self.address}...")
        reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), 10)
        sock = writer.get_extra_info("socket")
        if sock is not None:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.writer = writer
        writer.write(wire.frame(wire.join_message(wire.group_id(self.seed_hash, self.password), self.slot,
                                                  self.player)))
        pinger = asyncio.create_task(self.ping(writer))
        try:
            while not self.wake.is_set():
                read = asyncio.create_task(wire.read_frame(reader))
                woken = asyncio.create_task(self.wake.wait())
                done, _ = await asyncio.wait((read, woken), timeout=SILENT_TIMEOUT,
                                             return_when=asyncio.FIRST_COMPLETED)
                woken.cancel()
                if read not in done:
                    read.cancel()
                    if not self.wake.is_set():
                        raise asyncio.TimeoutError("the relay went silent")
                    return
                self.handle(read.result())
                if self.joined and self.pending is not None and self.flush_handle is None:
                    self.queue(self.pending)
                await writer.drain()
        finally:
            pinger.cancel()

    async def ping(self, writer: asyncio.StreamWriter) -> None:
        while True:
            await asyncio.sleep(PING_INTERVAL)
            writer.write(wire.frame(bytes([wire.PING])))


class RelayRefused(Exception):
    pass
