"""PC client: bridges the Archipelago server and the game's exlaunch module (TCP line protocol, see DESIGN.md)."""
import asyncio
from pathlib import Path
from typing import Dict, Optional, Set

import Utils
from CommonClient import ClientCommandProcessor, CommonContext, get_base_parser, gui_enabled, logger, server_loop
from NetUtils import ClientStatus

from .container import LASContainer
from .ids import LOCATION_BASE
from .install import InstallError, mod_dir, patch_and_install, start_game
from .items import GAME, ITEM_GAME_INFO

GAME_PORT = 43384
PROTOCOL_VERSION = 2
PING_INTERVAL = 2.0
RESEND_INTERVAL = 3.0


def clean(text: str) -> str:
    """Names travel inside a line, separated by '|'."""
    return " ".join(text.replace("|", "/").split())


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

    def _cmd_resync(self) -> None:
        """Ask the game to resend its received-item count and collected locations."""
        self.ctx.send_game("SYNC")

    def _cmd_patch(self, patch_file: str = "") -> None:
        """Patch and install the mod again, from the given .aplas or the one this client was opened with."""
        if patch_file:
            self.ctx.patch_file = patch_file
        if not self.ctx.patch_file:
            logger.info("No patch file; use /patch <path to .aplas>.")
            return
        asyncio.create_task(self.ctx.install_patch(force=True))


class LASContext(CommonContext):
    game = GAME
    # Remote items: the server sends this world's own items too. Starting inventory is granted by the patch.
    items_handling = 0b011
    command_processor = LASCommandProcessor

    def __init__(self, server_address: Optional[str], password: Optional[str], game_host: str, game_port: int,
                 patch_file: Optional[str] = None):
        super().__init__(server_address, password)
        self.patch_file = patch_file
        self.game_host = game_host
        self.game_port = game_port
        self.game_writer: Optional[asyncio.StreamWriter] = None
        self.game_seed_hash: Optional[str] = None
        self.game_recv: Optional[int] = None
        self.slot_seed_hash: Optional[str] = None
        self.game_checked: Set[int] = set()
        self.goal_reached = False
        self.last_item_sent = 0.0
        # Local location id -> "<item name>|<owner name, empty for our own items>", from LocationScouts.
        self.scouted_names: Dict[int, str] = {}

    async def server_auth(self, password_requested: bool = False) -> None:
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict) -> None:
        if cmd == "Connected":
            self.slot_seed_hash = args["slot_data"].get("seed_hash")
            self.check_seed()
            asyncio.create_task(self.flush_checks())
            asyncio.create_task(self.send_msgs([{"cmd": "LocationScouts", "locations": sorted(self.server_locations),
                                                 "create_as_hint": 0}]))
            if args["slot_data"].get("death_link"):
                asyncio.create_task(self.update_death_link(True))
        elif cmd == "ReceivedItems":
            self.send_next_item()
        elif cmd == "LocationInfo":
            for item in args["locations"]:
                if item.location < LOCATION_BASE:
                    continue
                owner = "" if item.player == self.slot else self.player_names.get(item.player, "")
                name = self.item_names.lookup_in_slot(item.item, item.player)
                self.scouted_names[item.location - LOCATION_BASE] = f"{clean(name)}|{clean(owner)}"
            self.send_location_names()

    def on_deathlink(self, data: Dict) -> None:
        super().on_deathlink(data)
        self.send_game("DEATH")

    def check_seed(self) -> bool:
        if self.slot_seed_hash and self.game_seed_hash and self.slot_seed_hash != self.game_seed_hash:
            logger.error(f"The game runs seed {self.game_seed_hash} but this slot needs {self.slot_seed_hash}. "
                         f"Install the mod generated for this slot.")
            return False
        return True

    def send_game(self, line: str) -> None:
        if self.game_writer is not None:
            self.game_writer.write(line.encode() + b"\n")

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
        if not self.server or not self.slot:
            return
        new = self.game_checked - self.locations_checked
        if new:
            await self.check_locations(new)
        if self.goal_reached and not self.finished_game:
            await self.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            self.finished_game = True

    def handle_game_line(self, line: str) -> None:
        parts = line.split()
        if not parts:
            return
        cmd = parts[0]
        if cmd == "HELLO":
            self.game_seed_hash = parts[2] if len(parts) > 2 else None
            if len(parts) < 2 or parts[1] != str(PROTOCOL_VERSION):
                logger.error(f"Game protocol {parts[1:2]} does not match the client ({PROTOCOL_VERSION}).")
            logger.info(f"Connected to the game (seed hash {self.game_seed_hash}).")
            self.check_seed()
            self.send_location_names()
            self.send_game("SYNC")
        elif cmd == "RECV":
            self.game_recv = int(parts[1])
            self.send_next_item()
        elif cmd in ("CHECK", "CHECKS"):
            ids = {LOCATION_BASE + int(p) for p in parts[1:]}
            if self.check_seed():
                self.game_checked |= ids
                asyncio.create_task(self.flush_checks())
        elif cmd == "GOAL":
            self.goal_reached = True
            asyncio.create_task(self.flush_checks())
        elif cmd == "DIED":
            if "DeathLink" in self.tags:
                asyncio.create_task(self.send_death(f"{self.player_names.get(self.slot, 'Link')} fell on Koholint."))
        elif cmd == "LOG":
            logger.debug("game: " + line[4:])
        elif cmd == "ERR":
            logger.warning("game: " + line)
        else:
            logger.info("game: " + line)

    async def game_loop(self) -> None:
        while not self.exit_event.is_set():
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(self.game_host, self.game_port), timeout=5)
            except (OSError, asyncio.TimeoutError):
                await asyncio.sleep(2)
                continue
            self.game_writer = writer
            self.game_recv = None
            ping = asyncio.create_task(self.ping_loop(writer))
            try:
                while not self.exit_event.is_set():
                    raw = await reader.readline()
                    if not raw:
                        break
                    line = raw.decode(errors="replace").strip()
                    if line and line != "PONG":
                        self.handle_game_line(line)
            except (ConnectionError, OSError):
                pass
            finally:
                ping.cancel()
                self.game_writer = None
                writer.close()
                logger.info("Lost connection to the game; retrying.")

    async def ping_loop(self, writer: asyncio.StreamWriter) -> None:
        loop = asyncio.get_event_loop()
        while True:
            writer.write(b"PING\n")
            await writer.drain()
            # An ITEM can be dropped while the player is in a cutscene; resend it.
            if self.game_recv is not None and loop.time() - self.last_item_sent > RESEND_INTERVAL:
                self.send_next_item()
            await asyncio.sleep(PING_INTERVAL)

    async def install_patch(self, force: bool = False, start: bool = False) -> None:
        """Patches the game with `patch_file` and installs the mod, using the paths in host.yaml (las_options)."""
        import settings
        from . import LASWorld
        options = LASWorld.settings
        try:
            container = LASContainer(self.patch_file)
            container.read()
            romfs, sd_card = Path(options.romfs_folder), Path(options.sd_card_folder)
        except FileNotFoundError as e:
            logger.error(f"Cannot patch: {e}. Set the folders in las_options in host.yaml.")
            return
        except Exception as e:
            logger.error(f"Cannot read {self.patch_file}: {e}")
            return
        if settings.get_settings().changed:
            settings.get_settings().save()
        loop = asyncio.get_running_loop()

        def log(line: str) -> None:
            loop.call_soon_threadsafe(logger.info, line)

        try:
            await asyncio.to_thread(patch_and_install, options.patcher_command, Path(self.patch_file),
                                    container.patch["seed_hash"], romfs, sd_card,
                                    mod_dir(sd_card, options.mod_folder), log, force)
        except (InstallError, OSError) as e:
            logger.error(f"Patching failed: {e}")
            return
        if start:
            start_game(options.game_start, logger.info)

    def make_gui(self):
        ui = super().make_gui()
        ui.base_title = "Archipelago Link's Awakening (Switch) Client"
        return ui


def launch(*launch_args: str) -> None:
    async def main(args) -> None:
        ctx = LASContext(args.connect, args.password, args.game_host, args.game_port, args.patch)
        ctx.auth = args.name
        ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
        game_task = asyncio.create_task(ctx.game_loop(), name="game loop")
        if gui_enabled and not args.nogui:
            ctx.run_gui()
        ctx.run_cli()
        if args.patch:
            asyncio.create_task(ctx.install_patch(start=True), name="patch")
        await ctx.exit_event.wait()
        game_task.cancel()
        await ctx.shutdown()

    parser = get_base_parser(description="Link's Awakening (Switch) Archipelago client.")
    parser.add_argument("--name", default=None, help="Slot name to connect as.")
    parser.add_argument("--game-host", default=None,
                        help="Address of the Switch or emulator running the game (default: las_options.game_host).")
    parser.add_argument("--game-port", type=int, default=GAME_PORT)
    if not hasattr(parser.parse_args([]), "nogui"):
        parser.add_argument("--nogui", default=False, action="store_true")
    parser.add_argument("url", nargs="?", help="Archipelago connection url, or an .aplas patch file to install")
    args = parser.parse_args(launch_args)
    args.patch = None
    if args.url and args.url.lower().endswith(LASContainer.patch_file_ending):
        # Opened with a patch file: install its mod, then connect to the server and slot it names.
        args.patch, args.url = args.url, None
        container = LASContainer(args.patch)
        try:
            container.read()
            args.connect = args.connect or container.server or None
            args.name = args.name or container.player_name or None
        except Exception:
            pass  # install_patch reports it
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
