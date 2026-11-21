"""DeathLink: the patched game over flow (patcher ap_deathlink.py) and the client's DIED and DEATH lines."""
import asyncio
import itertools
import unittest

from . import client_context, forget, patcher_import, romfs

ROMFS = romfs()
COMMON_EVENT = ROMFS / "region_common" / "event" / "Common.bfevfl"


@unittest.skipUnless(COMMON_EVENT.exists(), "RomFS dump not found (LASAP_ROMFS)")
class TestGameOverFlow(unittest.TestCase):
    """The game over after the patch, for each received death, shopkeeper's revenge, Secret Medicine and Chamber
    Dungeon state."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.evfl, cls.ap_deathlink = patcher_import("evfl", "ap_deathlink", modbuild=True)

    @classmethod
    def tearDownClass(cls) -> None:
        forget("ap_deathlink", "RandomizerCore")

    def read(self):
        flow = self.evfl.EventFlow()
        flow.read(COMMON_EVENT.read_bytes())
        return flow.flowchart

    def run_flow(self, chart, received: bool, revenge: bool, medicine: bool, panel: bool) -> list:
        """The actions and subflows the GameOver entry runs, in order."""
        from evfl.event import ActionEvent, SubFlowEvent, SwitchEvent
        flags = {self.ap_deathlink.DEATHLINK_FLAG: received, "ShopKeeperCounterattack": revenge}
        entry = next(ep for ep in chart.entry_points if ep.name == self.ap_deathlink.ENTRY)
        steps = []
        event = entry.main_event.v
        while event is not None:
            data = event.data
            if isinstance(data, ActionEvent):
                steps.append((data.actor_action.v.v, dict(data.params.data) if data.params else {}))
                event = data.nxt.v
            elif isinstance(data, SubFlowEvent):
                steps.append(("subflow", data.entry_point_name))
                event = data.nxt.v
            elif isinstance(data, SwitchEvent):
                query, params = data.actor_query.v.v, data.params.data if data.params else {}
                if query == "CheckFlag":
                    value = flags[params["symbol"]]
                elif query == "HasItem":
                    self.assertEqual(params["itemType"], self.ap_deathlink.MEDICINE_ITEM_TYPE)
                    value = medicine
                elif query == "CheckPanelDungeon":
                    value = panel
                else:
                    self.fail(f"unexpected query {query} at {event.name}")
                event = data.cases[int(value)].v
            else:
                self.fail(f"unexpected event {event.name}")
        return steps

    def test_game_over(self) -> None:
        original = self.read()
        chart = self.read()
        self.ap_deathlink.patch_game_over(chart)
        died = ("SetFlag", {"symbol": self.ap_deathlink.DIED_FLAG, "value": True})
        cleared = ("SetFlag", {"symbol": self.ap_deathlink.DEATHLINK_FLAG, "value": False})
        for revenge, medicine, panel in itertools.product((False, True), repeat=3):
            with self.subTest(revenge=revenge, medicine=medicine, panel=panel):
                vanilla = self.run_flow(original, False, revenge, medicine, panel)
                own = self.run_flow(chart, False, revenge, medicine, panel)
                received = self.run_flow(chart, True, revenge, medicine, panel)
                # An own death runs as before and is reported when it stands.
                self.assertEqual([s for s in own if s not in (died, cleared)], vanilla)
                stands = revenge or not medicine
                self.assertEqual(own.count(died), int(stands))
                # A received death runs as before too, so the medicine saves Link from it; it is never reported, and
                # the flow clears its flag however it ends.
                self.assertEqual([s for s in received if s not in (died, cleared)], vanilla)
                self.assertNotIn(died, received)
                self.assertEqual(received.count(cleared), 1)


class TestClientDeathLink(unittest.TestCase):
    def context(self, tags):
        class Server:
            socket = object()
        return client_context(game=True, slot=1, player_names={1: "Link"}, tags=set(tags), server=Server())

    def run_died(self, tags) -> list:
        async def run() -> list:
            ctx, sent, _ = self.context(tags)
            ctx.handle_game_line("DIED")
            await asyncio.sleep(0)
            await asyncio.sleep(0)
            return sent
        return asyncio.run(run())

    def test_died_sends_death(self) -> None:
        sent = self.run_died({"AP", "DeathLink"})
        self.assertEqual(len(sent), 1)
        self.assertEqual(sent[0]["cmd"], "Bounce")
        self.assertEqual(sent[0]["tags"], ["DeathLink"])
        self.assertEqual(sent[0]["data"]["source"], "Link")
        self.assertTrue(sent[0]["data"]["cause"])

    def test_died_without_deathlink(self) -> None:
        self.assertEqual(self.run_died({"AP"}), [])

    def test_received_death_reaches_game(self) -> None:
        async def run() -> list:
            ctx, _, lines = self.context({"AP", "DeathLink"})
            ctx.on_deathlink({"time": 1.0, "source": "Finder", "cause": "Finder fell."})
            return lines
        self.assertEqual(asyncio.run(run()), ["DEATH Finder"])


class TestDeathLinkCommand(unittest.TestCase):
    """/deathlink turns DeathLink on or off for the slot and keeps that in data storage; the slot's death_link option
    holds until then."""

    def context(self, option: bool):
        class Socket:
            closed = False

        class Server:
            socket = Socket()
        return client_context(slot=1, team=0, slot_data={"death_link": option},
                              tags={"AP", "DeathLink"} if option else {"AP"}, server=Server())

    def command(self, ctx, value: str) -> None:
        from ..client import LASCommandProcessor
        LASCommandProcessor(ctx)._cmd_deathlink(value)

    @staticmethod
    def messages(sent) -> list:
        return [(m["cmd"], set(m["tags"])) if m["cmd"] == "ConnectUpdate" else
                (m["cmd"], m["key"], m["operations"][0]["value"]) for m in sent]

    def test_toggle(self) -> None:
        async def run():
            ctx, sent, _ = self.context(False)
            self.assertFalse(ctx.death_link_on())
            self.command(ctx, "on")
            await asyncio.sleep(0)
            on = (ctx.death_link_on(), set(ctx.tags), self.messages(sent))
            sent.clear()
            self.command(ctx, "OFF")
            await asyncio.sleep(0)
            return on, (ctx.death_link_on(), set(ctx.tags), self.messages(sent))
        on, off = asyncio.run(run())
        self.assertEqual(on, (True, {"AP", "DeathLink"}, [("ConnectUpdate", {"AP", "DeathLink"}),
                                                           ("Set", "las_deathlink_0_1", True)]))
        self.assertEqual(off, (False, {"AP"}, [("ConnectUpdate", {"AP"}), ("Set", "las_deathlink_0_1", False)]))

    def test_stored_choice_overrides_option(self) -> None:
        async def run(stored):
            ctx, _, _ = self.context(True)
            ctx.on_package("Retrieved", {"keys": {ctx.deathlink_key: stored}})
            await asyncio.sleep(0)
            return ctx.death_link_on(), "DeathLink" in ctx.tags
        self.assertEqual(asyncio.run(run(False)), (False, False))
        self.assertEqual(asyncio.run(run(None)), (True, True))

    def test_needs_slot_and_value(self) -> None:
        async def run():
            ctx, sent, _ = self.context(False)
            self.command(ctx, "maybe")
            ctx.slot = None
            self.command(ctx, "on")
            await asyncio.sleep(0)
            return ctx.death_link_stored, sent
        self.assertEqual(asyncio.run(run()), (None, []))
