"""A second ghost player for tests: joins a ghost relay as another player of a run, walks a small circle somewhere,
and logs what the relay sends it (DESIGN.md, Ghosts).

python scripts/ghost_peer.py --relay 127.0.0.1:43390 --secret <ghost secret> [--slot Peer]
                             [--level Field] [--at x,y,z] [--radius 1.5] [--seconds 60]
Logs "PLAYER <id> <slot>", "FRAME <id> <level> <x> <y> <z> <yaw> ..." (the first of each player, then every 20th;
every n-th with --every n) and "OFF <id> <reason>" lines. stdin takes "goto <level> <x> <y> <z>", "radius <r>" (0:
stand still, facing +z), "shapes <hex>", "tunic <n>", "face <eye> <mouth> <ear>", "upper <anim id>" (what it sends,
ghost_relay.py's Pos), "nowhere", "burst <every ms> <hold ms>" (holds its frames back for the first hold ms of
every every ms, then sends them together, as TCP delivers the lines behind a lost packet; "burst 0 0" ends it) and
"link <jitter ms> <loss %> <stall ms>" (a bad link: each frame waits a random time, jitter on average, and that
percentage of them stall ms more, delivered in order as TCP does; "link 0 0 0" ends it) while it runs.
"record <file> [<slot>]" writes the frames one other player sends
(that slot's, or the first to send one; "<wall clock ms> <the position as a keyframe in hex>" per line; two peers'
files sorted together make one, as a player goes from one area to another) until "record stop"; "play <file>" sends
such frames as its own, on their timing and its clock, then stands at the last ("PLAYED <frames>").
"""
import argparse
import asyncio
import importlib.util
import math
import random
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
    password, reader, writer = await wire.connect(args.relay)
    writer.write(wire.frame(wire.join_message(wire.group_id(args.secret), args.slot, args.slot, password)))
    place = {"level": args.level, "at": [float(v) for v in args.at.split(",")], "here": True, "radius": args.radius,
             "shapes": wire.SHAPES_DEFAULT, "tunic": wire.TUNIC_GREEN, "face": (0, 0, 0), "upper": 0}
    names = {}
    counts = {}
    positions = {}  # the last position of each player received, from its frames' changes
    sent = {"pos": None}  # the last position sent, which the next one's changes are against
    recording: dict = {"file": None, "slot": None, "player": None}
    playing = {"frames": [], "start": 0.0, "next": 0}
    burst: dict = {"every": 0, "hold": 0, "held": []}  # frames held back (burst <every ms> <hold ms>; 0 0: none)
    # link <jitter ms> <loss %> <stall ms>, and when the last frame sent through it goes out (event loop time)
    link = {"jitter": 0.0, "loss": 0.0, "stall": 0.0, "due": 0.0}
    rng = random.Random(1)

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
                p = positions[player_id] = wire.decode_pos(payload[3:], positions.get(player_id))
                if recording["file"] is not None and recording["player"] is None and                         recording["slot"] in (None, names.get(player_id)):
                    recording["player"] = player_id
                if recording["file"] is not None and player_id == recording["player"]:
                    recording["file"].write(f"{time.time() * 1000:.0f} {wire.encode_pos(p).hex()}\n")
                counts[player_id] = counts.get(player_id, 0) + 1
                if counts[player_id] % args.every == 1 % args.every:
                    name = wire.LEVELS[p.level] if p.level < len(wire.LEVELS) else p.level
                    log(f"FRAME {player_id} {name} {p.x:.2f} {p.y:.2f} {p.z:.2f} {p.yaw * 360 / 65536:.0f} "
                        f"anim={p.anim} upper={p.upper_anim} flags={p.flags} shapes={p.shapes:x} tunic={p.tunic} "
                        f"face={p.eye},{p.mouth},{p.ear} n={counts[player_id]} t={p.time_ms}")
            elif kind == wire.OFF:
                player_id, reason = struct.unpack_from(">HB", payload, 1)
                counts.pop(player_id, None)
                positions.pop(player_id, None)
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
            elif len(parts) == 3 and parts[0] == "burst":
                burst.update(every=int(parts[1]), hold=int(parts[2]))
                log(f"BURST {parts[1]} {parts[2]}")
            elif len(parts) == 4 and parts[0] == "link":
                link.update(jitter=float(parts[1]), loss=float(parts[2]), stall=float(parts[3]))
                log(f"LINK {' '.join(parts[1:])}")
            elif parts == ["record", "stop"] and recording["file"] is not None:
                recording["file"].close()
                recording["file"] = None
                log("RECORDED")
            elif len(parts) in (2, 3) and parts[0] == "record":
                recording.update(file=open(parts[1], "w"), slot=parts[2] if len(parts) == 3 else None, player=None)
                log(f"RECORD {' '.join(parts[1:])}")
            elif len(parts) == 2 and parts[0] == "play":
                with open(parts[1]) as f:
                    frames = [(int(ms), wire.decode_pos(bytes.fromhex(body), None)) for ms, body in
                              (line.split() for line in f if line.strip())]
                if frames:
                    frames = [(ms - frames[0][0], p) for ms, p in frames]
                    playing.update(frames=frames, start=time.monotonic(), next=0)
                    log(f"PLAY {parts[1]} {len(frames)}")

    def send(payload: bytes) -> None:
        """Writes a frame, or holds it back for the first `hold` ms of every `every` (burst), then writes the held ones
        with the next: delivered as TCP delivers the lines behind a lost packet."""
        data = wire.frame(payload)
        if burst["every"] and time.monotonic() * 1000 % burst["every"] < burst["hold"]:
            burst["held"].append(data)
            return
        deliver(b"".join(burst["held"]) + data)
        burst["held"].clear()

    def deliver(data: bytes) -> None:
        """Writes bytes after the link's delay (link), never ahead of bytes written before them."""
        loop = asyncio.get_running_loop()
        now = loop.time()
        delay = rng.expovariate(1 / link["jitter"]) if link["jitter"] else 0.0
        if rng.random() * 100 < link["loss"]:
            delay += link["stall"]
        if delay == 0 and link["due"] <= now:
            writer.write(data)
            return
        link["due"] = max(link["due"] + 0.0001, now + delay / 1000)
        loop.call_at(link["due"], writer.write, data)

    def send_pos(pos) -> None:
        """A POS frame: the changes since the last position sent (a keyframe after none)."""
        send(bytes([wire.POS]) + wire.encode_pos(pos, sent["pos"]))
        sent["pos"] = pos

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
                send_pos(p._replace(time_ms=(base + p.time_ms) & 0xFFFF))
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
            send_pos(wire.Pos(int(t * 1000) & 0xFFFF, wire.LEVEL_IDS[place["level"]], x + radius * math.cos(angle), y,
                              z + radius * math.sin(angle), yaw,
                              0, 0, place["upper"], 0 if radius else wire.FLAG_STILL, place["shapes"], place["tunic"],
                              *place["face"]).quantized())
        else:
            send(bytes([wire.NOWHERE]))
            sent["pos"] = None
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
