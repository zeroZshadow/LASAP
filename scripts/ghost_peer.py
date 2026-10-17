"""A second ghost player for tests: joins a ghost relay as another player of a run, walks a small circle somewhere,
and logs what the relay sends it (DESIGN.md, Ghosts).

python scripts/ghost_peer.py --relay 127.0.0.1:43390 --secret <ghost secret> [--slot Peer]
                             [--level Field] [--at x,y,z] [--radius 1.5] [--seconds 60]
Logs "PLAYER <id> <slot>", "FRAME <id> <level> <x> <y> <z> <yaw> ..." (the first of each player, then every 20th;
every n-th with --every n) and "OFF <id> <reason>" lines. stdin takes "goto <level> <x> <y> <z>", "radius <r>" (0:
stand still, facing +z), "shapes <hex>", "tunic <n>", "face <eye> <mouth> <ear>", "upper <anim id>" (what it sends,
ghost_relay.py's Pos) and "nowhere" while it runs; "record <file>" writes the frames another player sends ("<wall
clock ms> <frame body in hex>" per line; two peers' files sorted together make one, as a player goes from one area to
another) until "record stop", and "play <file>" sends such frames as its own, on their timing and its clock, then
stands at the last ("PLAYED <frames>").
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
    password, host, port = wire.parse_address(args.relay)
    reader, writer = await asyncio.open_connection(host, port)
    writer.write(wire.frame(wire.join_message(wire.group_id(args.secret), args.slot, args.slot, password)))
    place = {"level": args.level, "at": [float(v) for v in args.at.split(",")], "here": True, "radius": args.radius,
             "shapes": wire.SHAPES_DEFAULT, "tunic": wire.TUNIC_GREEN, "face": (0, 0, 0), "upper": 0}
    names = {}
    counts = {}
    recording: dict = {"file": None}
    playing = {"frames": [], "start": 0.0, "first": 0, "next": 0}

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
                p = wire.Pos.unpack(payload[3:])
                if recording["file"] is not None:
                    recording["file"].write(f"{time.time() * 1000:.0f} {payload[3:].hex()}\n")
                counts[player_id] = counts.get(player_id, 0) + 1
                if counts[player_id] % args.every == 1 % args.every:
                    name = wire.LEVELS[p.level] if p.level < len(wire.LEVELS) else p.level
                    log(f"FRAME {player_id} {name} {p.x:.2f} {p.y:.2f} {p.z:.2f} {p.yaw * 360 / 65536:.0f} "
                        f"anim={p.anim} upper={p.upper_anim} flags={p.flags} shapes={p.shapes:x} tunic={p.tunic} "
                        f"face={p.eye},{p.mouth},{p.ear} n={counts[player_id]} t={p.time_ms}")
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
            elif len(parts) == 2 and parts[0] == "radius":
                place["radius"] = float(parts[1])
                log(f"RADIUS {parts[1]}")
            elif len(parts) == 2 and parts[0] == "shapes":
                place["shapes"] = int(parts[1], 16)
                log(f"SHAPES {parts[1]}")
            elif len(parts) == 2 and parts[0] == "tunic":
                place["tunic"] = int(parts[1])
                log(f"TUNIC {parts[1]}")
            elif len(parts) == 4 and parts[0] == "face":
                place["face"] = tuple(int(v) for v in parts[1:4])
                log(f"FACE {' '.join(parts[1:4])}")
            elif len(parts) == 2 and parts[0] == "upper":
                place["upper"] = int(parts[1])
                log(f"UPPER {parts[1]}")
            elif parts == ["nowhere"]:
                place["here"] = False
                log("NOWHERE")
            elif parts == ["record", "stop"] and recording["file"] is not None:
                recording["file"].close()
                recording["file"] = None
                log("RECORDED")
            elif len(parts) == 2 and parts[0] == "record":
                recording["file"] = open(parts[1], "w")
                log(f"RECORD {parts[1]}")
            elif len(parts) == 2 and parts[0] == "play":
                with open(parts[1]) as f:
                    frames = [(int(ms), wire.Pos.unpack(bytes.fromhex(body))) for ms, body in
                              (line.split() for line in f if line.strip())]
                if frames:
                    frames = [(ms - frames[0][0], p) for ms, p in frames]
                    playing.update(frames=frames, start=time.monotonic(), next=0)
                    log(f"PLAY {parts[1]} {len(frames)}")

    receiver = asyncio.create_task(receive())
    if not args.no_stdin:
        asyncio.create_task(commands())
    start = time.monotonic()
    while time.monotonic() - start < args.seconds and not receiver.done():
        if playing["frames"]:
            # recorded frames when they arrived, their sender's clock (frames that arrived together keep their own
            # times) moved to this one's; then the last one, standing there
            frames, now = playing["frames"], time.monotonic()
            base = int((playing["start"] - start) * 1000) - frames[0][1].time_ms  # this clock less the sender's
            while playing["next"] < len(frames) and frames[playing["next"]][0] <= (now - playing["start"]) * 1000:
                p = frames[playing["next"]][1]
                writer.write(wire.frame(bytes([wire.POS]) + p._replace(time_ms=(base + p.time_ms) & 0xFFFF).pack()))
                playing["next"] += 1
            if playing["next"] == len(frames):
                last = frames[-1][1]
                place.update(level=wire.LEVELS[last.level], at=[last.x, last.y, last.z], here=True, radius=0)
                playing["frames"] = []
                log(f"PLAYED {len(frames)}")
            await writer.drain()
            await asyncio.sleep(0.005)
            continue
        if place["here"]:
            t = time.monotonic() - start
            x, y, z = place["at"]
            angle = t * 1.5  # along (cos, sin) in x, z: heading -angle (yaw 0 = +z, 90 = +x)
            radius = place["radius"]
            yaw = int((-angle * 180 / math.pi) % 360 * 65536 / 360) & 0xFFFF if radius else 0  # still: facing +z
            body = wire.Pos(int(t * 1000) & 0xFFFF, wire.LEVEL_IDS[place["level"]], x + radius * math.cos(angle), y,
                            z + radius * math.sin(angle), yaw,
                            0, 0, place["upper"], 0 if radius else wire.FLAG_STILL, place["shapes"], place["tunic"],
                            *place["face"]).pack()
            writer.write(wire.frame(bytes([wire.POS]) + body))
        else:
            writer.write(wire.frame(bytes([wire.NOWHERE])))
        await writer.drain()
        await asyncio.sleep(0.05)
    writer.close()
    if receiver.done() and receiver.exception():
        log(f"CLOSED {receiver.exception()!r}")
    log("DONE")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--relay", default=f"127.0.0.1:{wire.DEFAULT_PORT}")
    parser.add_argument("--secret", required=True, help="the multiworld's ghost secret (patch.json ghost_secret)")
    parser.add_argument("--slot", default="Peer")
    parser.add_argument("--level", default="Field")
    parser.add_argument("--at", default="38.25,10.5,126", help="x,y,z the circle is around")
    parser.add_argument("--radius", type=float, default=1.5)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--no-stdin", action="store_true")
    parser.add_argument("--every", type=int, default=20, help="log every n-th frame of each player (1: all)")
    asyncio.run(main(parser.parse_args()))
