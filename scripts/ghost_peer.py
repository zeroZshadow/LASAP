"""A second ghost player for tests: joins a ghost relay as another player of a run, walks a small circle somewhere,
and logs what the relay sends it (DESIGN.md, Ghosts).

python scripts/ghost_peer.py --relay 127.0.0.1:43390 --seed <seed hash> [--password pw] [--slot Peer]
                             [--level Field] [--at x,y,z] [--radius 1.5] [--seconds 60]
Logs "PLAYER <id> <slot>", "FRAME <id> <level> <x> <y> <z> <yaw>" (the first of each player, then every 20th) and
"OFF <id> <reason>" lines. stdin takes "goto <level> <x> <y> <z>" and "nowhere" while it runs.
"""
import argparse
import asyncio
import importlib.util
import math
import struct
import sys
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "ghost_relay", Path(__file__).resolve().parent.parent / "apworld" / "las" / "ghost_relay.py")
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)


def log(text: str) -> None:
    print(text, flush=True)


async def main(args) -> None:
    host, _, port = args.relay.rpartition(":")
    reader, writer = await asyncio.open_connection(host, int(port))
    writer.write(wire.frame(wire.join_message(wire.group_id(args.seed, args.password), args.slot, args.slot)))
    place = {"level": args.level, "at": [float(v) for v in args.at.split(",")], "here": True}
    names = {}
    counts = {}

    async def receive() -> None:
        while True:
            payload = await wire.read_frame(reader)
            kind = payload[0]
            if kind == wire.WELCOME:
                log(f"WELCOME {struct.unpack_from('>H', payload, 2)[0]}")
            elif kind == wire.PLAYER:
                player_id = struct.unpack_from(">H", payload, 1)[0]
                names[player_id] = wire.read_name(payload, 3)[0]
                log(f"PLAYER {player_id} {names[player_id]}")
            elif kind == wire.FRAME:
                player_id = struct.unpack_from(">H", payload, 1)[0]
                _, level, room, x, y, z, yaw, anim, anim_ms, flags = wire.POS_BODY.unpack(payload[3:])
                counts[player_id] = counts.get(player_id, 0) + 1
                if counts[player_id] % 20 == 1:
                    name = wire.LEVELS[level] if level < len(wire.LEVELS) else level
                    log(f"FRAME {player_id} {name} {x:.2f} {y:.2f} {z:.2f} {yaw * 360 / 65536:.0f} room={room} "
                        f"flags={flags} n={counts[player_id]}")
            elif kind == wire.OFF:
                player_id, reason = struct.unpack_from(">HB", payload, 1)
                counts.pop(player_id, None)
                log(f"OFF {player_id} {'left' if reason == wire.OFF_LEFT else 'area'}")
            elif kind == wire.ERROR:
                log("ERROR " + payload[1:].decode("utf-8", "replace"))

    async def commands() -> None:
        loop = asyncio.get_running_loop()
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                return
            parts = line.split()
            if len(parts) == 5 and parts[0] == "goto":
                place.update(level=parts[1], at=[float(v) for v in parts[2:5]], here=True)
                log(f"AT {' '.join(parts[1:])}")
            elif parts == ["nowhere"]:
                place["here"] = False
                log("NOWHERE")

    receiver = asyncio.create_task(receive())
    if not args.no_stdin:
        asyncio.create_task(commands())
    start = time.monotonic()
    seq = 0
    while time.monotonic() - start < args.seconds and not receiver.done():
        if place["here"]:
            t = time.monotonic() - start
            x, y, z = place["at"]
            angle = t * 1.5
            body = wire.POS_BODY.pack(seq & 0xFFFF, wire.LEVEL_IDS[place["level"]],
                                      wire.room_index(place["level"], x, z), x + args.radius * math.cos(angle), y,
                                      z + args.radius * math.sin(angle), int((angle * 180 / math.pi + 90) % 360
                                                                                * 65536 / 360) & 0xFFFF, 0, 0, 0)
            writer.write(wire.frame(bytes([wire.POS]) + body))
        else:
            writer.write(wire.frame(bytes([wire.NOWHERE])))
        seq += 1
        await writer.drain()
        await asyncio.sleep(0.05)
    writer.close()
    if receiver.done() and receiver.exception():
        log(f"CLOSED {receiver.exception()!r}")
    log("DONE")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--relay", default=f"127.0.0.1:{wire.DEFAULT_PORT}")
    parser.add_argument("--seed", required=True)
    parser.add_argument("--password", default="")
    parser.add_argument("--slot", default="Peer")
    parser.add_argument("--level", default="Field")
    parser.add_argument("--at", default="38.25,10.5,126", help="x,y,z the circle is around")
    parser.add_argument("--radius", type=float, default=1.5)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--no-stdin", action="store_true")
    asyncio.run(main(parser.parse_args()))
