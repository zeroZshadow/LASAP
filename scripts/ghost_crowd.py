"""A crowd of ghost players for load tests: each joins a ghost relay as a player of a run and runs between random
points of its area, as a player's game sends (DESIGN.md, Ghosts).

python scripts/ghost_crowd.py --relay 127.0.0.1:43390 --secret <ghost secret>
                              --crowd "20 Field 5,110,58,142 10.5" --crowd "10 CraneGame 3,6,12,8.5 0" [--seconds 600]
A crowd is "<count> <level> <x0>,<z0>,<x1>,<z1> <y>": its players run (4.5 to 6.5 units/s, the module's run gait)
inside that rectangle, stop for a moment now and then, and wear a random tunic. Each connects from its own loopback
address (127.0.0.2 and up; the relay takes 16 connections per address) unless --one-address. Prints "READY <n>" once
all are welcomed, then every --report seconds and at the end "STATS" with the frames sent and received.
"""
import argparse
import asyncio
import importlib.util
import math
import random
import struct
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "ghost_relay", Path(__file__).resolve().parent.parent / "apworld" / "las" / "ghost_relay.py")
assert spec is not None and spec.loader is not None
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)

MOVING_INTERVAL, STILL_INTERVAL = 0.05, 0.5  # as the game sends: every 50 ms while moving, 500 ms while still
SPEED = (4.5, 6.5)
PAUSE_CHANCE, PAUSE = 0.3, (0.5, 2.5)  # at a waypoint: the chance to stand still, and for how long


class Walker:
    def __init__(self, name: str, level: str, area, y: float, rng: random.Random):
        self.name, self.level, self.area, self.y, self.rng = name, level, area, y, rng
        self.x, self.z = self.point()
        self.target = self.point()
        self.speed = rng.uniform(*SPEED)
        self.yaw = 0.0
        self.still_until = 0.0
        self.look = (wire.LOOK_DEFAULT & ~(3 << 20)) | (rng.randrange(3) << 20)
        self.sent = self.received = 0
        self.players = set()
        self.welcomed = False
        self.error = ""

    def point(self):
        x0, z0, x1, z1 = self.area
        return self.rng.uniform(x0, x1), self.rng.uniform(z0, z1)

    def step(self, now: float, dt: float) -> bool:
        """Moves on; True while moving."""
        if now < self.still_until:
            return False
        dx, dz = self.target[0] - self.x, self.target[1] - self.z
        dist = math.hypot(dx, dz)
        if dist < 0.1:
            self.target = self.point()
            self.speed = self.rng.uniform(*SPEED)
            if self.rng.random() < PAUSE_CHANCE:
                self.still_until = now + self.rng.uniform(*PAUSE)
            return False
        move = min(dist, self.speed * dt)
        self.x += dx / dist * move
        self.z += dz / dist * move
        self.yaw = math.degrees(math.atan2(dx, dz)) % 360  # yaw 0 = +z, 90 = +x
        return True

    def body(self, t_ms: int, moving: bool) -> bytes:
        return wire.POS_BODY.pack(t_ms & 0xFFFF, wire.LEVEL_IDS[self.level], wire.room_index(self.level, self.x, self.z),
                                  self.x, self.y, self.z, int(self.yaw * 65536 / 360) & 0xFFFF, 0, 0,
                                  0 if moving else wire.FLAG_STILL, self.look)


async def run(walker: Walker, host: str, port: int, source: str, secret: str, deadline: float) -> None:
    try:
        reader, writer = await asyncio.open_connection(host, port, local_addr=(source, 0) if source else None)
    except OSError as e:
        walker.error = repr(e)
        return
    writer.write(wire.frame(wire.join_message(wire.group_id(secret), walker.name, walker.name)))

    async def receive() -> None:
        while True:
            payload = await wire.read_frame(reader)
            kind = payload[0]
            if kind == wire.WELCOME:
                walker.welcomed = True
            elif kind == wire.FRAME:
                walker.received += 1
                walker.players.add(struct.unpack_from(">H", payload, 1)[0])
            elif kind == wire.OFF:
                walker.players.discard(struct.unpack_from(">H", payload, 1)[0])
            elif kind == wire.ERROR:
                walker.error = payload[1:].decode("utf-8", "replace")

    receiver = asyncio.create_task(receive())
    start = last = time.monotonic()
    next_send = start
    was_moving = False
    try:
        while time.monotonic() < deadline and not receiver.done():
            now = time.monotonic()
            moving = walker.step(now, now - last)
            last = now
            if now >= next_send or moving != was_moving:  # starting or stopping goes out at once, as in the game
                was_moving = moving
                writer.write(wire.frame(bytes([wire.POS]) + walker.body(int((now - start) * 1000), moving)))
                walker.sent += 1
                next_send = now + (MOVING_INTERVAL if moving else STILL_INTERVAL)
                await writer.drain()
            await asyncio.sleep(MOVING_INTERVAL)
    except OSError as e:
        walker.error = repr(e)
    receiver.cancel()
    writer.close()


def stats(walkers, started: float) -> str:
    secs = max(0.001, time.monotonic() - started)
    sent = sum(w.sent for w in walkers)
    received = sum(w.received for w in walkers)
    seen = sum(len(w.players) for w in walkers) / max(1, len(walkers))
    errors = sum(1 for w in walkers if w.error)
    return (f"STATS t={secs:.0f}s players={len(walkers)} welcomed={sum(w.welcomed for w in walkers)} "
            f"sent={sent} ({sent / secs:.0f}/s) received={received} ({received / secs:.0f}/s) "
            f"in-area={seen:.1f} errors={errors}")


async def main(args) -> None:
    host, _, port = args.relay.rpartition(":")
    rng = random.Random(args.seed)
    walkers = []
    for spec_text in args.crowd:
        count, level, area, y = spec_text.split()
        rect = [float(v) for v in area.split(",")]
        for i in range(int(count)):
            walkers.append(Walker(f"{level[:8]}{i + 1:02d}", level, rect, float(y), rng))
    deadline = time.monotonic() + args.seconds
    tasks = [asyncio.create_task(run(w, host, int(port), "" if args.one_address else f"127.0.0.{2 + n}",
                                     args.secret, deadline)) for n, w in enumerate(walkers)]
    started = time.monotonic()
    while not all(w.welcomed or w.error for w in walkers) and time.monotonic() < started + 10:
        await asyncio.sleep(0.05)
    print(f"READY {sum(w.welcomed for w in walkers)}", flush=True)
    for w in walkers:
        if w.error:
            print(f"ERROR {w.name} {w.error}", flush=True)
    next_report = time.monotonic() + args.report
    while not all(t.done() for t in tasks):
        await asyncio.sleep(0.2)
        if time.monotonic() >= next_report:
            print(stats(walkers, started), flush=True)
            next_report += args.report
    print(stats(walkers, started), flush=True)
    print("DONE", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--relay", default=f"127.0.0.1:{wire.DEFAULT_PORT}")
    parser.add_argument("--secret", required=True, help="the multiworld's ghost secret (patch.json ghost_secret)")
    parser.add_argument("--crowd", action="append", required=True, help='"<count> <level> <x0>,<z0>,<x1>,<z1> <y>"')
    parser.add_argument("--seconds", type=float, default=600)
    parser.add_argument("--report", type=float, default=10)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--one-address", action="store_true", help="connect all from the default address")
    asyncio.run(main(parser.parse_args()))
