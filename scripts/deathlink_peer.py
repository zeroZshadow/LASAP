"""A second DeathLink player for tests: a text-only client on another slot with the DeathLink tag.

python scripts/deathlink_peer.py --connect localhost:<port> --name <slot>   (in .venv-ap; stdin takes commands)
  /die [cause]   sends a DeathLink from this slot
Received DeathLinks are logged as "DeathLink: ..." lines (CommonContext.on_deathlink), so tests can count echoes.
"""
import asyncio
import logging

from ap import setup

setup()
from CommonClient import ClientCommandProcessor, CommonContext, get_base_parser, server_loop  # noqa: E402


class PeerCommandProcessor(ClientCommandProcessor):
    def _cmd_die(self, *cause: str) -> None:
        """Send a DeathLink from this slot."""
        asyncio.create_task(self.ctx.send_death(" ".join(cause) or "Peer died."))


class PeerContext(CommonContext):
    tags = CommonContext.tags | {"TextOnly", "DeathLink"}
    game = ""
    items_handling = 0
    want_slot_data = False
    command_processor = PeerCommandProcessor

    async def server_auth(self, password_requested: bool = False):
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect(game="")


async def main(args) -> None:
    ctx = PeerContext(args.connect, args.password)
    ctx.auth = args.name
    ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
    ctx.run_cli()
    await ctx.exit_event.wait()
    await ctx.shutdown()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s", force=True)
    parser = get_base_parser(description=__doc__)
    parser.add_argument("--name", required=True)
    asyncio.run(main(parser.parse_args()))
