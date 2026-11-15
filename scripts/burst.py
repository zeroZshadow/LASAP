"""Captures the sandbox emulator's window as fast as it can (about 140 frames a second at 1280x720, more than the
game's 60) for a while, for what lasts a frame or two (a ghost's arrival, a menu opening): the frames stay in memory
(3.7 MB each) and are written as PNGs afterwards, named <index>_<ms since the start>.png.

Run with .venv-lasr (needs Pillow):
  python scripts/burst.py <out dir> <seconds> [--at <s>] [--dbg "<DBG command>"] [--peer "<peer command>"]
                          [--crop x0,y0,x1,y1] [--fps <n>] [--video <file.mp4>]
--dbg sends a DBG command on the debug port (e2e.py's) and --peer appends a command to the session's peer_cmds.txt
(LASAP_E2E, ghost_peer.py's stdin), --at seconds after the capture starts (default 0.3); it prints when.
--crop keeps only that part of each frame. --fps keeps at most that many frames a second (a video of a few seconds
needs no more than the game's 60), which keeps the writing short; the PNGs are written with fast compression.
--video writes the frames as an H.264 video through ffmpeg (on PATH) instead of PNGs, at their average rate: seconds
for what takes a PNG per frame a minute; pull single frames out of it with ffmpeg.
"""
import argparse
import os
import subprocess
import sys
import threading
import time

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import e2e  # noqa: E402
import emu  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("seconds", type=float)
    parser.add_argument("--at", type=float, default=0.3)
    parser.add_argument("--dbg")
    parser.add_argument("--peer")
    parser.add_argument("--crop")
    parser.add_argument("--fps", type=float)
    parser.add_argument("--video")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    hwnd = emu.game_window(int(open(emu.PIDFILE).read().strip()))
    if hwnd is None:
        raise SystemExit("no emulator window")
    capture = emu.Capture(hwnd)
    w, h = capture.w, capture.h
    crop = tuple(int(v) for v in args.crop.split(",")) if args.crop else None
    debug = e2e.Debug(5) if args.dbg else None
    sent = []

    def send():
        time.sleep(args.at)
        if debug is not None:
            debug.send("DBG " + args.dbg)
        if args.peer:
            with open(os.path.join(str(e2e.DIR), "peer_cmds.txt"), "a") as f:
                f.write(args.peer + "\n")
        sent.append(time.perf_counter())
    if args.dbg or args.peer:
        threading.Thread(target=send, daemon=True).start()
    frames = []
    start = time.perf_counter()
    step = 1 / args.fps if args.fps else 0
    while time.perf_counter() - start < args.seconds:
        if step:
            wait = start + len(frames) * step - time.perf_counter()
            if wait > 0:
                time.sleep(wait)
        frames.append((capture.frame(), time.perf_counter() - start))
    capture.close()
    if sent:
        print(f"command at {(sent[0] - start) * 1000:.0f} ms")
    if args.video:
        x0, y0, x1, y1 = crop or (0, 0, w, h)
        rate = len(frames) / args.seconds
        ffmpeg = subprocess.Popen(
            ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgra", "-s", f"{w}x{h}",
             "-framerate", f"{rate:.3f}", "-i", "-", "-vf", f"crop={x1 - x0}:{y1 - y0}:{x0}:{y0}", "-c:v", "libx264",
             "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p", args.video], stdin=subprocess.PIPE)
        for buf, _ in frames:
            ffmpeg.stdin.write(buf.raw)
        ffmpeg.stdin.close()
        if ffmpeg.wait() != 0:
            raise SystemExit("ffmpeg failed")
        print(f"{len(frames)} frames in {args.seconds} s ({rate:.1f}/s) -> {args.video}")
        return
    for n, (buf, t) in enumerate(frames):
        im = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")
        (im.crop(crop) if crop else im).save(os.path.join(args.out, f"{n:04d}_{t * 1000:05.0f}.png"), compress_level=1)
    print(f"{len(frames)} frames in {args.seconds} s -> {args.out}")


if __name__ == "__main__":
    main()
