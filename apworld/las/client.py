"""PC client: bridges the Archipelago server and the game's exlaunch module (TCP line protocol, see DESIGN.md)."""
import asyncio
import socket
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

import Utils
from CommonClient import ClientCommandProcessor, CommonContext, get_base_parser, gui_enabled, logger, server_loop
from NetUtils import ClientStatus, HintStatus

from .container import LASContainer
from .ids import LOCATION_BASE, LOCATION_OFFSETS
from . import checklist, emulators, ghost_relay
from .ghosts import DEFAULT_RELAY, GhostLink
from .host_settings import PLAYER_SETTINGS, player_value
from .install import InstallError, patch_and_install
from .items import GAME, ITEM_GAME_INFO
from .tracker import SIGN_KEY, LogicTracker, tracked_instruments

GAME_PORT = 43384
PROTOCOL_VERSION = 2
PING_INTERVAL = 2.0
RESEND_INTERVAL = 3.0
HELLO_TIMEOUT = 5.0
# Servers before this have no CreateHints; on them, in-game hints of this slot's own locations become hints by scouting
CREATE_HINTS_VERSION = Utils.Version(0, 6, 2)
# The game takes one client at a time; another client's connection waits without an answer (also for the 10 s the
# game takes to drop a client that went away).
BUSY_STATUS = ("Another client is connected to the game. If another Links Awakening Switch Client window is open, "
               "close it.")
# Data storage key holding the level the game shows (LASR level names: Field, Lv01TailCave, ...), for trackers
LEVEL_KEY = "las_level_{team}_{slot}"


def clean(text: str) -> str:
    """Names travel inside a line, separated by '|'."""
    return " ".join(text.replace("|", "/").split())


OPTION_KEYS = ("emulator", "game_file", "romfs_folder", "sd_card_folder", "mod_folder", "patcher_command", "game_host")
GHOST_KEYS = ("ghost_relay",)


class LASCommandProcessor(ClientCommandProcessor):
    ctx: "LASContext"

    def _cmd_las(self) -> None:
        """Show the connection state to the game."""
        ctx = self.ctx
        if ctx.game_writer is None:
            logger.info(f"Not connected to the game ({ctx.game_host}:{ctx.game_port}).")
        else:
            logger.info(f"Connected to the game. Seed hash {ctx.game_seed_hash}, "
                        f"{ctx.game_recv} of {len(ctx.items_received)} items applied.")

    def _cmd_dbg(self, *args: str) -> None:
        """Send a debug command to the game module (testing only), e.g. /dbg state."""
        self.ctx.send_game("DBG " + " ".join(args))

    def _cmd_game(self, *args: str) -> None:
        """Send a raw protocol line to the game module (testing only)."""
        self.ctx.send_game(" ".join(args))

    def _cmd_ghosts(self, value: str = "") -> None:
        """Show the ghosts' state, or turn them on or off: /ghosts on|off."""
        ctx = self.ctx
        if value:
            if value.lower() not in ("on", "off"):
                logger.error("Usage: /ghosts [on|off]")
                return
            ctx.set_ghosts(value.lower() == "on")
        link = ctx.ghosts
        logger.info(f"{link.status.text or link.waiting_for()} Relay {link.address or '(none)'}, "
                    f"{link.sent_frames} frames sent, {link.received_frames} received, ghost slots {link.ghosts}.")

    def _cmd_ghostnames(self, value: str = "") -> None:
        """Show whether name tags show over the ghosts, or turn them on or off: /ghostnames on|off."""
        ctx = self.ctx
        if value:
            if value.lower() not in ("on", "off"):
                logger.error("Usage: /ghostnames [on|off]")
                return
            ctx.set_ghost_names(value.lower() == "on")
        logger.info(f"Ghost name tags are {'on' if ctx.ghost_names_on() else 'off'}.")

    def _cmd_ghostrelay(self, address: str = "") -> None:
        """Show the ghost relay in use, or set the Ghost relay setting: /ghostrelay <address> (host:port,
        password@host:port or wss://host), /ghostrelay default for the default relay."""
        ctx = self.ctx
        if address:
            if address.lower() == "default":
                address = DEFAULT_RELAY
            else:
                try:
                    ghost_relay.parse_address(address)
                except ValueError:
                    logger.error("Usage: /ghostrelay [host:port | password@host:port | wss://host | default]")
                    return
            ctx.set_option("ghost_relay", address)
        link = ctx.ghosts
        if not link.address:
            logger.info("Ghost relay: none.")
            return
        source = (" (the default relay)" if link.default_relay else
                  " (from the patch file)" if not ctx.option("ghost_relay") and ctx.ghost_relay_arg is None else "")
        logger.info(f"Ghost relay: {ghost_relay.shown_address(link.address)}{source}.")

    def _cmd_resync(self) -> None:
        """Ask the game to resend its received-item count and collected locations."""
        self.ctx.send_game("SYNC")

    def _cmd_patch(self, patch_file: str = "") -> None:
        """Patch and install the mod again, from the given .aplas or the one this client was opened with."""
        if patch_file:
            self.ctx.set_patch_file(patch_file)
        asyncio.create_task(self.ctx.install_patch(force=True))

    def _cmd_play(self) -> None:
        """Install the mod when it is missing or out of date, then start the emulator with the game."""
        asyncio.create_task(self.ctx.install_patch(start=True))

    def _cmd_start(self) -> None:
        """Start the emulator with the game."""
        asyncio.create_task(self.ctx.start_emulator())

    def _cmd_setting(self, key: str = "", value: str = "") -> None:
        """Show the look and sound settings, or set one, e.g. /setting music Shuffled or /setting blur_removal on."""
        settings = {s.key: s for s in PLAYER_SETTINGS}
        if not key:
            for s in PLAYER_SETTINGS:
                value = self.ctx.player_option(s.key)
                shown = ("on" if value else "off") if s.choices is None else value
                logger.info(f"{s.key}: {shown} ({'/'.join(s.choices or ('on', 'off'))}) - {s.label}")
            return
        if key not in settings or not value:
            logger.error("Usage: /setting <name> <value>, with a name from /setting.")
            return
        setting = settings[key]
        if setting.choices is None:
            if value.lower() not in ("on", "off", "true", "false"):
                logger.error(f"{key} is on or off.")
                return
            self.ctx.set_player_option(key, value.lower() in ("on", "true"))
        else:
            choice = next((c for c in setting.choices if c.lower() == value.lower()), None)
            if choice is None:
                logger.error(f"{key} is one of {', '.join(setting.choices)}.")
                return
            self.ctx.set_player_option(key, choice)
        logger.info(self.ctx.check_setup().checks["installed"].text)

    def _cmd_setup(self) -> None:
        """Show the patch settings and what is missing (the Patch tab has the same)."""
        checks = self.ctx.check_setup()
        for key in OPTION_KEYS:
            check = checks.checks.get(key)
            note = f" - {check.text}" if check and check.text else ""
            logger.info(f"{key}: {self.ctx.option(key) or '(empty)'}{note}")
        logger.info(checks.checks["installed"].text)


class LASContext(CommonContext):
    game = GAME
    # Remote items: the server sends this world's own items too. Starting inventory is granted by the patch.
    items_handling = 0b011
    command_processor = LASCommandProcessor

    def __init__(self, server_address: Optional[str], password: Optional[str], game_host: str, game_port: int,
                 patch_file: Optional[str] = None):
        super().__init__(server_address, password)
        self.patch_file = patch_file
        self.patch_slot = ""  # slot name from the patch file
        self.game_status = checklist.Check(checklist.INFO, "Waiting for the game to start.")
        self.patching = False  # an install runs; the Patch tab's buttons are off
        self.notice_sent = False  # the game was told the server connection state
        container = self.read_patch_file()
        self.patch_slot = container.player_name if container else ""
        self.game_host = game_host
        self.game_port = game_port
        self.game_writer: Optional[asyncio.StreamWriter] = None
        # The game's port took the connection but the game did not answer: it serves another client
        self.game_busy = False
        self.game_probed = asyncio.Event()  # the game loop tried to connect once
        self.emulator_process: Optional["subprocess.Popen[bytes]"] = None  # the emulator this client started
        self.game_seed_hash: Optional[str] = None
        self.game_recv: Optional[int] = None
        self.slot_seed_hash: Optional[str] = None
        self.game_checked: Set[int] = set()
        # In-game hints the game reports as read, (slot, location id), and those sent to the server since connecting
        self.game_hints: Set[Tuple[int, int]] = set()
        self.hints_sent: Set[Tuple[int, int]] = set()
        self.goal_reached = False
        self.game_level: Optional[str] = None  # the level the game shows, from its LOG level lines
        self.last_item_sent = 0.0
        # Local location id -> "<item name>|<owner name, empty for our own items>", from LocationScouts.
        self.scouted_names: Dict[int, str] = {}
        self.logic_tracker: Optional[LogicTracker] = None  # the slot's rules, for the map tracker
        self.in_logic: Optional[Set[int]] = None  # local ids last sent to the game as in logic
        self.logic_starting: List[str] = []  # the slot's starting items, which the server does not send
        self.slot_data: Dict[str, Any] = {}
        self.logic_tracker_instruments = 8  # the instruments the map tracker's rules take for the egg
        self.sign_seed: Optional[str] = None  # the seed whose game reported the sign at the egg's stairs read (SIGN)
        self.ghosts = GhostLink(self.send_game, self.refresh_patch_tab)
        self.ghost_relay_arg: Optional[str] = None  # --ghost-relay replaces las_options.ghost_relay
        self.ghost_secret_arg: Optional[str] = None  # --ghost-secret replaces the patch file's (tests)

    async def get_username(self) -> None:
        # the slot comes from the patch file, as the server would otherwise ask for it
        if not self.auth and self.patch_slot:
            self.auth = self.patch_slot
        await super().get_username()

    def set_patch_file(self, path: Optional[str]) -> None:
        """Takes the slot, and the server when the patch names one, from the patch file."""
        self.patch_file = path or None
        container = self.read_patch_file()
        self.patch_slot = container.player_name if container else ""
        if container is None or not self.patch_slot:
            return
        if self.input_requests > 0 and not self.slot:  # the server asked for the slot name
            self.input_requests -= 1
            self.input_queue.put_nowait(self.patch_slot)
        elif self.slot and self.auth != self.patch_slot:
            logger.warning(f"Connected as {self.auth}, but the patch file is for {self.patch_slot}.")
        if container.server and not self.server:
            asyncio.create_task(self.connect(container.server))
        self.update_ghosts()

    def read_patch_file(self) -> Optional[LASContainer]:
        if not self.patch_file:
            return None
        container = LASContainer(self.patch_file)
        try:
            container.read()
        except Exception:
            return None
        return container

    async def server_auth(self, password_requested: bool = False) -> None:
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict) -> None:
        if cmd == "Connected":
            self.slot_seed_hash = args["slot_data"].get("seed_hash")
            self.notice_sent = False
            self.check_seed()
            self.send_notice()
            self.send_server_state()
            self.refresh_patch_tab()
            asyncio.create_task(self.flush_checks())
            self.hints_sent.clear()
            asyncio.create_task(self.flush_hints())
            asyncio.create_task(self.send_msgs([{"cmd": "LocationScouts", "locations": sorted(self.server_locations),
                                                 "create_as_hint": 0}]))
            if args["slot_data"].get("death_link"):
                asyncio.create_task(self.update_death_link(True))
            asyncio.create_task(self.send_level())
            self.send_checked(self.checked_locations)
            self.slot_data = args["slot_data"]
            self.make_logic_tracker()
            asyncio.create_task(self.send_sign_read())
            # read in an earlier session: the game's save may have lost it (quit without saving)
            asyncio.create_task(self.send_msgs([{"cmd": "Get", "keys": [self.sign_key]}]))
            self.send_logic()
            self.update_ghosts()
        elif cmd == "RoomUpdate":
            self.send_checked(args.get("checked_locations", []))
            self.send_logic()
        elif cmd == "ReceivedItems":
            self.send_next_item()
            self.send_logic()
        elif cmd == "Retrieved" and args.get("keys", {}).get(self.sign_key) is True:
            self.on_sign_read(self.slot_seed_hash)
        elif cmd == "LocationInfo":
            for item in args["locations"]:
                if item.location < LOCATION_BASE:
                    continue
                owner = "" if item.player == self.slot else self.player_names.get(item.player, "")
                name = self.item_names.lookup_in_slot(item.item, item.player)
                self.scouted_names[item.location - LOCATION_BASE] = f"{clean(name)}|{clean(owner)}"
            self.send_location_names()

    def make_logic_tracker(self) -> None:
        """The slot's rules for the map tracker. A count of instruments left to chance stays hidden until the player
        has read the sign (tracked_instruments)."""
        logic = self.slot_data.get("logic")
        names = {offset: loc for loc, offset in LOCATION_OFFSETS.items()}
        instruments = tracked_instruments(self.slot_data.get("instruments_required", 8),
                                          self.slot_data.get("instruments_known", True), self.sign_read)
        self.logic_tracker = LogicTracker(
            logic["preset"], logic["tokens"], logic["vanilla"],
            [names[loc - LOCATION_BASE] for loc in sorted(self.server_locations)],
            self.slot_data.get("dungeon_entrances"), logic.get("optional", ()), instruments) if logic else None
        self.logic_tracker_instruments = instruments
        self.logic_starting = logic.get("starting", []) if logic else []
        self.in_logic = None

    @property
    def sign_read(self) -> bool:
        """Whether the player has read the sign at the egg's stairs in the slot's seed."""
        return self.sign_seed is not None and self.sign_seed == self.slot_seed_hash

    @property
    def sign_key(self) -> str:
        return SIGN_KEY.format(team=self.team, player=self.slot)

    def on_sign_read(self, seed_hash: Optional[str]) -> None:
        """The player has read the sign at the egg's stairs in this seed: the game says so, or data storage does."""
        if seed_hash is None:
            return
        if self.sign_seed != seed_hash:
            logger.info("The sign at the foot of the egg's stairs has been read: trackers show the instruments "
                        "the egg needs.")
        self.sign_seed = seed_hash
        required = self.slot_data.get("instruments_required", 8)
        if self.slot_data and self.sign_read and self.logic_tracker_instruments != required:
            self.make_logic_tracker()
            self.send_logic()

    async def send_sign_read(self) -> None:
        """Puts the sign's read in the server's data storage, where trackers follow it (SIGN_KEY)."""
        if not self.sign_read or self.slot is None:
            return
        await self.send_msgs([{"cmd": "Set", "key": self.sign_key,
                               "default": False, "want_reply": False,
                               "operations": [{"operation": "replace", "value": True}]}])

    def on_deathlink(self, data: Dict) -> None:
        super().on_deathlink(data)
        self.send_game(f"DEATH {clean(str(data.get('source', '')))}".rstrip())

    def send_notice(self) -> None:
        """Tells the player in the game, once per connection, whether the client is connected to their slot."""
        if self.game_writer is None or self.game_seed_hash is None or not self.slot or self.notice_sent:
            return
        self.notice_sent = True
        # the popup holds two lines of about 22 characters
        slot = clean(self.player_names.get(self.slot, self.auth or ""))[:16]
        if self.slot_seed_hash and self.slot_seed_hash != self.game_seed_hash:
            self.send_game(f"NOTICE Wrong seed for {slot}|Reinstall the patch")
        else:
            self.send_game(f"NOTICE Archipelago connected|as {slot}")

    async def connection_closed(self) -> None:
        if self.slot and self.game_writer is not None:
            self.send_game("NOTICE Archipelago server|disconnected")
        self.notice_sent = False
        await super().connection_closed()
        self.send_server_state()
        self.refresh_patch_tab()

    def send_server_state(self) -> None:
        """Tells the game whether the client is logged into the slot of the game's seed; the game reminds the player
        while it is not."""
        online = bool(self.server and self.slot) and not (
            self.slot_seed_hash and self.game_seed_hash and self.slot_seed_hash != self.game_seed_hash)
        self.send_game(f"SERVER {int(online)}")

    @property
    def server_status(self) -> checklist.Check:
        """The server connection, for the Patch tab."""
        if self.server and self.slot:
            name = self.player_names.get(self.slot, self.auth or "")
            return checklist.Check(checklist.OK, f"Connected to the Archipelago server as {name}.")
        if self.server:
            return checklist.Check(checklist.INFO, "Connecting to the Archipelago server...")
        return checklist.Check(checklist.TODO, "Not connected to the Archipelago server: enter the room's address "
                                               "(such as archipelago.gg:38281) at the top and press Connect.")

    def check_seed(self) -> bool:
        if self.slot_seed_hash and self.game_seed_hash and self.slot_seed_hash != self.game_seed_hash:
            self.set_game_status(checklist.ERROR, f"The game runs another seed ({self.game_seed_hash}) than slot "
                                 f"{self.auth} ({self.slot_seed_hash}). Press Play, then restart the game.")
            logger.error(f"The game runs another seed ({self.game_seed_hash}) than this slot ({self.slot_seed_hash}). "
                         f"In the Patch tab, choose this slot's patch file and press Play, then restart "
                         f"the game.")
            return False
        return True

    def send_game(self, line: str) -> None:
        if self.game_writer is not None:
            self.game_writer.write(line.encode() + b"\n")

    def send_checked(self, locations) -> None:
        """Tells the game which locations the server has as checked, for the map tracker."""
        if not self.slot or (self.slot_seed_hash and self.game_seed_hash and self.slot_seed_hash != self.game_seed_hash):
            return
        ids = sorted(loc - LOCATION_BASE for loc in locations if loc >= LOCATION_BASE)
        for i in range(0, len(ids), 64):
            self.send_game("CHECKED " + " ".join(str(local_id) for local_id in ids[i:i + 64]))

    def send_logic(self, force: bool = False) -> None:
        """Tells the game which locations are in logic for the items received, when that changed."""
        if self.logic_tracker is None or self.game_writer is None or not self.slot:
            return
        if self.slot_seed_hash and self.game_seed_hash and self.slot_seed_hash != self.game_seed_hash:
            return
        names = list(self.logic_starting) + [self.item_names.lookup_in_game(item.item, self.game)
                                             for item in self.items_received]
        ids = {LOCATION_OFFSETS[loc] for loc in self.logic_tracker.in_logic(names)}
        if ids == self.in_logic and not force:
            return
        self.in_logic = ids
        self.send_game("INLOGIC CLEAR")
        ordered = sorted(ids)
        for i in range(0, len(ordered), 64):
            self.send_game("INLOGIC " + " ".join(str(local_id) for local_id in ordered[i:i + 64]))

    def send_location_names(self) -> None:
        for local_id, names in sorted(self.scouted_names.items()):
            self.send_game(f"NAME {local_id} {names}")

    def send_next_item(self) -> None:
        """Sends the next item the game has not applied yet; the game answers RECV with its new count."""
        if self.game_writer is None or self.game_recv is None or not self.check_seed():
            return
        if self.game_recv >= len(self.items_received):
            return
        item = self.items_received[self.game_recv]
        name = self.item_names.lookup_in_game(item.item, self.game)
        key, dungeon = ITEM_GAME_INFO.get(name, ("", -1))
        if not key:
            logger.warning(f"No game item for {name}; skipping it.")
            key = "None"
        # Our own items carry the location they came from; other players' items name the sender.
        own = item.player == self.slot
        location = item.location - LOCATION_BASE if own and item.location >= LOCATION_BASE else -1
        sender = "" if own or item.player == 0 else clean(self.player_names.get(item.player, ""))
        self.send_game(f"ITEM {self.game_recv + 1} {key} 1 {dungeon} {location} {clean(name)}|{sender}")
        self.last_item_sent = asyncio.get_event_loop().time()

    async def flush_checks(self) -> None:
        # only into the slot of the seed the game runs: checks that came before the slot was known wait for it
        if not self.server or not self.slot or not self.slot_seed_hash or self.slot_seed_hash != self.game_seed_hash:
            return
        new = self.game_checked - self.locations_checked
        if new:
            await self.check_locations(new)
        if self.goal_reached and not self.finished_game:
            await self.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            self.finished_game = True

    async def flush_hints(self) -> None:
        """Makes the in-game hints read in the game server hints, as !hint would (DESIGN.md, In-game hints)."""
        if not self.server or not self.slot or not self.check_seed():
            return
        new = sorted(self.game_hints - self.hints_sent)
        if not new:
            return
        self.hints_sent.update(new)
        by_player: Dict[int, List[int]] = {}
        for player, location in new:
            by_player.setdefault(player, []).append(location)
        if self.server_version >= CREATE_HINTS_VERSION:
            await self.send_msgs([{"cmd": "CreateHints", "player": player, "locations": locations,
                                   "status": HintStatus.HINT_PRIORITY} for player, locations in by_player.items()])
        elif self.slot in by_player:
            await self.send_msgs([{"cmd": "LocationScouts", "locations": by_player[self.slot], "create_as_hint": 2}])

    def on_game_line(self, raw: bytes) -> None:
        """A line from the game; one the client cannot handle is logged and skipped, so it ends neither the game's
        connection nor the loop that connects again."""
        line = raw.decode(errors="replace").strip()
        if not line or line == "PONG":
            return
        try:
            self.handle_game_line(line)
        except Exception:
            logger.exception(f"Ignored a line from the game: {line[:200]}")

    def handle_game_line(self, line: str) -> None:
        parts = line.split()
        if not parts:
            return
        cmd = parts[0]
        if cmd == "HELLO":
            self.game_seed_hash = parts[2] if len(parts) > 2 else None
            # the game sends its checks again (CHECKS) after HELLO, for the seed it runs now
            self.game_hints.clear()
            self.game_checked.clear()
            self.goal_reached = False
            if len(parts) < 2 or parts[1] != str(PROTOCOL_VERSION):
                logger.error(f"Game protocol {parts[1:2]} does not match the client ({PROTOCOL_VERSION}).")
            logger.info(f"Connected to the game (seed hash {self.game_seed_hash}).")
            self.set_game_status(checklist.OK, f"Connected to the game (seed {self.game_seed_hash}).")
            self.check_seed()
            self.send_notice()
            self.send_server_state()
            self.send_location_names()
            self.send_checked(self.checked_locations)
            self.send_logic(force=True)
            self.send_game("SYNC")
            self.update_ghosts()
            self.ghosts.on_game_connected()
        elif cmd == "POS":
            self.ghosts.on_pos(parts)
        elif cmd == "GHOSTNAMES" and len(parts) == 2:
            # the player turned the name tags on or off in the game (d-pad up)
            self.set_ghost_names(parts[1] == "1")
            logger.info(f"Ghost name tags are {'on' if self.ghost_names_on() else 'off'}.")
        elif cmd == "RECV":
            self.game_recv = int(parts[1])
            self.send_next_item()
        elif cmd in ("CHECK", "CHECKS"):
            ids = {LOCATION_BASE + int(p) for p in parts[1:]}
            if self.check_seed():
                self.game_checked |= ids
                asyncio.create_task(self.flush_checks())
        elif cmd == "HINT" and len(parts) == 3:
            # HINT <slot> <location id>: the player read an in-game hint
            self.game_hints.add((int(parts[1]), int(parts[2])))
            asyncio.create_task(self.flush_hints())
        elif cmd == "GOAL":
            self.goal_reached = True
            asyncio.create_task(self.flush_checks())
        elif cmd == "SIGN":
            # the player read the sign naming the instruments the egg needs
            self.on_sign_read(self.game_seed_hash)
            asyncio.create_task(self.send_sign_read())
        elif cmd == "DIED":
            if "DeathLink" in self.tags:
                asyncio.create_task(self.send_death(f"{self.player_names.get(self.slot, 'Link')} fell on Koholint."))
        elif cmd == "DEATHDROPPED":
            logger.info("DeathLink: ignored, no save is loaded in the game.")
        elif cmd == "LOG":
            if len(parts) == 3 and parts[1] == "level":
                self.game_level = parts[2]
                asyncio.create_task(self.send_level())
            logger.debug("game: " + line[4:])
        elif cmd == "ERR":
            logger.warning("game: " + line)
        else:
            logger.info("game: " + line)

    async def send_level(self) -> None:
        """Puts the game's level in the server's data storage, where trackers follow it (LEVEL_KEY)."""
        if self.game_level is None or self.slot is None:
            return
        await self.send_msgs([{"cmd": "Set", "key": LEVEL_KEY.format(team=self.team, slot=self.slot), "default": "",
                               "want_reply": False, "operations": [{"operation": "replace", "value": self.game_level}]}])

    def set_game_status(self, level: str, text: str) -> None:
        self.game_status = checklist.Check(level, text)
        self.refresh_patch_tab()

    async def game_loop(self) -> None:
        failures = 0
        while not self.exit_event.is_set():
            writer = None
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(self.game_host, self.game_port), timeout=HELLO_TIMEOUT)
                sock = writer.get_extra_info("socket")
                if sock is not None:  # GHOST lines go out as written, not held back for an ACK
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                # the game says HELLO right away; while it serves another client, it takes no new one
                first = await asyncio.wait_for(reader.readline(), timeout=HELLO_TIMEOUT)
            except (OSError, asyncio.TimeoutError):
                busy = writer is not None
                if writer is not None:
                    writer.close()
                failures += 1
                if busy != self.game_busy or (failures % 5 == 1 and self.game_status.level != checklist.INFO):
                    self.game_busy = busy
                    if busy:
                        self.set_game_status(checklist.TODO, BUSY_STATUS)
                    else:
                        self.set_game_status(checklist.INFO, "Waiting for the game to start.")
                self.game_probed.set()
                await asyncio.sleep(2)
                continue
            failures = 0
            self.game_busy = False
            self.game_probed.set()
            self.game_writer = writer
            self.game_recv = None
            self.game_seed_hash = None
            self.notice_sent = False
            ping = asyncio.create_task(self.ping_loop(writer))
            try:
                self.on_game_line(first)
                while not self.exit_event.is_set():
                    try:
                        raw = await reader.readline()
                    except ValueError:  # a line longer than the stream's limit, which the reader dropped
                        logger.warning("Ignored a line from the game that is too long.")
                        continue
                    if not raw:
                        break
                    self.on_game_line(raw)
            except (ConnectionError, OSError):
                pass
            finally:
                ping.cancel()
                self.game_writer = None
                self.ghosts.on_game_lost()
                writer.close()
                logger.info("Lost connection to the game; retrying.")
                self.set_game_status(checklist.INFO, "Waiting for the game to start.")

    async def ping_loop(self, writer: asyncio.StreamWriter) -> None:
        loop = asyncio.get_event_loop()
        while True:
            writer.write(b"PING\n")
            await writer.drain()
            # An ITEM can be dropped while the player is in a cutscene; resend it.
            if self.game_recv is not None and loop.time() - self.last_item_sent > RESEND_INTERVAL:
                self.send_next_item()
            await asyncio.sleep(PING_INTERVAL)

    # Patching: settings live in host.yaml (las_options); the Patch tab shows and edits them.

    @staticmethod
    def option(key: str) -> str:
        from . import LASWorld
        return str(getattr(LASWorld.settings, key) or "")

    def set_option(self, key: str, value: str, detect: bool = True) -> None:
        import settings
        from . import LASWorld
        value = value.strip().strip('"')
        if self.option(key) == value:
            return
        options = LASWorld.settings
        setattr(options, key, type(getattr(options, key))(value))
        settings.get_settings().save()
        if key == "game_host":
            self.game_host = value or "127.0.0.1"
        if key == "emulator" and detect:
            self.detect_from_emulator()
        if key in GHOST_KEYS:
            self.update_ghosts()
        self.refresh_patch_tab()

    @staticmethod
    def player_option(key: str) -> Any:
        """A player setting (host_settings.PLAYER_SETTINGS) as the patcher takes it."""
        from . import LASWorld
        setting = next(s for s in PLAYER_SETTINGS if s.key == key)
        return player_value(setting, getattr(LASWorld.settings, key))

    def set_player_option(self, key: str, value: Any) -> None:
        import settings
        from . import LASWorld
        if self.player_option(key) == value:
            return
        setattr(LASWorld.settings, key, value)
        settings.get_settings().save()
        self.refresh_patch_tab()

    @staticmethod
    def player_settings() -> Dict[str, Any]:
        from . import LASWorld
        return LASWorld.settings.player_settings()

    @staticmethod
    def ghosts_on() -> bool:
        from . import LASWorld
        return bool(LASWorld.settings.ghosts)

    def set_ghosts(self, on: bool) -> None:
        import settings
        from . import LASWorld
        if self.ghosts_on() == on:
            return
        LASWorld.settings.ghosts = on
        settings.get_settings().save()
        self.update_ghosts()
        self.refresh_patch_tab()

    @staticmethod
    def ghost_names_on() -> bool:
        from . import LASWorld
        return bool(LASWorld.settings.ghost_names)

    def set_ghost_names(self, on: bool) -> None:
        import settings
        from . import LASWorld
        if self.ghost_names_on() == on:
            return
        LASWorld.settings.ghost_names = on
        settings.get_settings().save()
        self.ghosts.set_names(on)

    def update_ghosts(self) -> None:
        """Hands the ghost link its settings, the multiworld's ghost secret (from the patch file) and the slot; it
        reconnects when they change. The relay is the client's setting (DEFAULT_RELAY by default); when it is empty,
        the one the patch file was made with, else DEFAULT_RELAY."""
        from . import LASWorld
        options = LASWorld.settings
        slot = self.auth or self.patch_slot or ""
        player = self.player_names.get(self.slot, slot) if self.slot else slot
        container = self.read_patch_file()
        patch = container.patch if container else {}
        address = self.ghost_relay_arg
        if address is None:
            address = (str(options.ghost_relay or "").strip() or str(patch.get("ghost_relay", "")).strip() or
                       DEFAULT_RELAY)
        default_relay = address.strip() == DEFAULT_RELAY
        secret = self.ghost_secret_arg
        if secret is None:
            secret = str(patch.get("ghost_secret", ""))
        self.ghosts.configure(bool(options.ghosts), address, secret, slot, player, default_relay)
        self.ghosts.set_names(bool(options.ghost_names))

    @property
    def ghost_status(self) -> checklist.Check:
        """The ghosts' link to the relay, for the Patch tab."""
        return self.ghosts.status if self.ghosts.status.text else checklist.Check(checklist.INFO,
                                                                                    self.ghosts.waiting_for())

    def install_target(self) -> Optional[emulators.Target]:
        return emulators.target(self.option("emulator"), self.option("sd_card_folder"), self.option("mod_folder"))

    def check_setup(self) -> checklist.Checklist:
        return checklist.check(self.patch_file or "", {key: self.option(key) for key in OPTION_KEYS},
                               self.player_settings())

    def detect_from_emulator(self, game: bool = True) -> None:
        """Fills in the game file (unless not `game`) and RomFS dump the emulator knows about, when they are empty."""
        exe = self.option("emulator")
        if not exe:
            return
        target = self.install_target()
        data_dir = target.data_dir if target else None
        if game and not self.option("game_file"):
            game = emulators.find_game(emulators.game_dirs(exe, data_dir),
                                       recent=emulators.recent_games(exe, data_dir))
            if game:
                self.set_option("game_file", str(game), detect=False)
                logger.info(f"Found the game: {game}")
        if not self.option("romfs_folder"):
            romfs = emulators.find_romfs(exe, data_dir)
            if romfs:
                self.set_option("romfs_folder", str(romfs), detect=False)
                logger.info(f"Found the RomFS dump: {romfs}")

    def report(self, level: str, message: str, replace: bool = False) -> None:
        """Logs a patching step and adds it to the Patch tab's progress lines; `replace` replaces the last one."""
        getattr(logger, level)(message)
        if self.ui and hasattr(self.ui, "patch_panel"):
            self.ui.patch_panel.show_progress(level, message, replace)

    def show_patch_tab(self) -> None:
        if self.ui and hasattr(self.ui, "show_patch_tab"):
            self.ui.show_patch_tab()

    def refresh_patch_tab(self) -> None:
        if self.ui and hasattr(self.ui, "patch_panel"):
            self.ui.patch_panel.refresh()

    async def install_patch(self, force: bool = False, start: bool = False) -> None:
        """Installs the mod for `patch_file` unless the current one is installed (or `force`), then optionally starts
        the emulator. The Patch tab's buttons are off meanwhile."""
        if self.patching:
            return
        self.patching = True
        self.refresh_patch_tab()
        try:
            await self._install_patch(force, start)
        finally:
            self.patching = False
            self.refresh_patch_tab()

    async def _install_patch(self, force: bool, start: bool) -> None:
        if self.ui and hasattr(self.ui, "patch_panel"):
            self.ui.patch_panel.clear_progress()
        self.detect_from_emulator()  # a RomFS dump or game added since the client started
        checks = self.check_setup()
        if not checks.ready or checks.target is None:
            self.report("error", checks.problems())
            self.show_patch_tab()
            return
        target = checks.target
        if force or checks.checks["installed"].level != checklist.OK:
            loop = asyncio.get_running_loop()

            def log(line: str) -> None:
                loop.call_soon_threadsafe(logger.info, line)

            open_already = self.game_writer is not None
            self.report("info", f"Installing the mod for slot {checks.player_name}... This takes a few seconds.")
            logger.info(f"Seed {checks.seed_hash}, into {target.mods} and {target.sd_card}.")
            try:
                await asyncio.to_thread(patch_and_install, self.option("patcher_command"), Path(self.patch_file),
                                        checks.seed_hash, emulators.romfs_root(self.option("romfs_folder")),
                                        target.sd_card, target.mods, log, True, self.player_settings())
            except (InstallError, OSError) as e:
                self.report("error", f"Patching failed: {e} The Archipelago tab shows the patcher's output.", True)
                self.show_patch_tab()
                return
            self.report("info", f"Done: the mod for slot {checks.player_name} (seed {checks.seed_hash}) is installed.",
                        True)
            if open_already:
                self.report("warning", "The game is running: restart it to load the new mod.")
        else:
            self.report("info", f"The mod for slot {checks.player_name} (seed {checks.seed_hash}) is installed.")
        if "other_mods" in checks.checks:
            self.report("warning", checks.checks["other_mods"].text)
        if start:
            await self.start_emulator()

    async def start_emulator(self) -> None:
        exe = self.option("emulator")
        if not exe:
            self.report("error", "Choose your emulator in the Patch tab first.")
            self.show_patch_tab()
            return
        name = emulators.display_name(exe)
        try:  # whether the game runs already, from the game loop's first try
            await asyncio.wait_for(self.game_probed.wait(), timeout=10)
        except asyncio.TimeoutError:
            pass
        if self.game_writer is not None:
            self.report("info", "The game is running already.")
            return
        if self.game_busy:
            self.report("warning", "The game is running already. " + BUSY_STATUS)
            return
        if self.emulator_process is not None and self.emulator_process.poll() is None:
            self.report("info", f"{name} is open already. Start Link's Awakening there.")
            return
        target = self.install_target()
        game = self.option("game_file")
        args = emulators.start_args(exe, game, target.data_dir if target else None)
        import subprocess
        try:
            self.emulator_process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                                     stderr=subprocess.DEVNULL, cwd=str(Path(exe).parent))
        except OSError as e:
            self.report("error", f"Could not start {name}: {e}")
            return
        self.report("info", f"Started {name}." + ("" if game else " Start Link's Awakening from its game list."))
        logger.debug("Emulator command: " + " ".join(f'"{a}"' if " " in a else a for a in args))

    def make_gui(self):
        from .client_gui import make_manager
        return make_manager(super().make_gui())


async def prepare_patch(ctx: LASContext, patch: bool) -> None:
    """Once the window shows the log: finds the emulator and its folders, and shows the Patch tab when the client was
    opened with a patch file. Installing and starting wait for Play."""
    for _ in range(100):
        if not ctx.ui or hasattr(ctx.ui, "patch_tab"):
            break
        await asyncio.sleep(0.1)
    if patch:
        ctx.show_patch_tab()
    ctx.detect_from_emulator()
    ctx.refresh_patch_tab()


def launch(*launch_args: str) -> None:
    async def main(args) -> None:
        ctx = LASContext(args.connect, args.password, args.game_host, args.game_port, args.patch)
        ctx.auth = args.name
        ctx.ghost_relay_arg, ctx.ghost_secret_arg = args.ghost_relay, args.ghost_secret
        ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
        game_task = asyncio.create_task(ctx.game_loop(), name="game loop")
        ctx.update_ghosts()
        ghost_task = asyncio.create_task(ctx.ghosts.run(ctx.exit_event), name="ghosts")
        if gui_enabled and not args.nogui:
            ctx.run_gui()
        ctx.run_cli()
        if ctx.ui:
            asyncio.create_task(prepare_patch(ctx, bool(args.patch)), name="patch")
        await ctx.exit_event.wait()
        game_task.cancel()
        ghost_task.cancel()
        ctx.ghosts.drop()
        await ctx.shutdown()

    parser = get_base_parser(description="Link's Awakening (Switch) Archipelago client.")
    parser.add_argument("--name", default=None, help="Slot name to connect as.")
    parser.add_argument("--game-host", default=None,
                        help="Address of the Switch or emulator running the game (default: las_options.game_host).")
    parser.add_argument("--game-port", type=int, default=GAME_PORT)
    parser.add_argument("--ghost-relay", default=None, help="Ghost relay [password@]host:port or [password@]wss://host "
                                                              "(default: las_options.ghost_relay).")
    parser.add_argument("--ghost-secret", default=None,
                        help="The multiworld's ghost secret, for tests (default: the patch file's).")
    if not hasattr(parser.parse_args([]), "nogui"):
        parser.add_argument("--nogui", default=False, action="store_true")
    parser.add_argument("url", nargs="?", help="Archipelago connection url, or an .aplas patch file to install")
    args = parser.parse_args(launch_args)
    args.patch = None
    if args.url and args.url.lower().endswith(LASContainer.patch_file_ending):
        # Opened with a patch file: connect to the server and slot it names; Play installs its mod.
        args.patch, args.url = args.url, None
        container = LASContainer(args.patch)
        try:
            container.read()
            args.connect = args.connect or container.server or None
            args.name = args.name or container.player_name or None
        except Exception:
            pass  # check_setup reports it
    if args.url:
        from CommonClient import handle_url_arg
        args = handle_url_arg(args, parser=parser)
    if args.game_host is None:
        from . import LASWorld
        args.game_host = LASWorld.settings.game_host

    Utils.init_logging("LASClient", exception_logger="Client")
    import colorama
    colorama.just_fix_windows_console()
    asyncio.run(main(args))
    colorama.deinit()
