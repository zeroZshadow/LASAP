"""Scripted session against the LAS AP module: python drive.py <script.txt>
Script lines: 'send <line>' | 'wait <secs>' | 'shot <png>' | 'expect <regex> [secs]' | '# comment'"""
import re, socket, subprocess, sys, time, os
HERE = os.path.dirname(os.path.abspath(__file__))

# The sandbox game's port (deploy_mod.sh), so tests run beside a modded game on the default port.
SANDBOX_PORT = int(os.environ.get("LASAP_GAME_PORT", "43385"))

class Conn:
    def __init__(self, host="127.0.0.1", port=SANDBOX_PORT):
        deadline = time.time() + 60
        while True:
            try:
                self.s = socket.create_connection((host, port), timeout=2); break
            except OSError:
                if time.time() > deadline: raise
                time.sleep(1)
        self.s.settimeout(0.1); self.buf = b""; self.lines = []; self.last_ping = 0; self.consumed = 0
    def send(self, line):
        print(">", line, flush=True); self.s.sendall(line.encode() + b"\n")
    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            if time.time() - self.last_ping > 2:
                self.s.sendall(b"PING\n"); self.last_ping = time.time()
            try:
                d = self.s.recv(4096)
                if not d: raise ConnectionError("closed")
                self.buf += d
            except socket.timeout:
                pass
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1); t = l.decode(errors="replace")
                if t != "PONG": print("<", t, flush=True); self.lines.append(t)
    def expect(self, rx, secs):
        """Waits for a line matching rx among lines not consumed by an earlier expect."""
        r = re.compile(rx); end = time.time() + secs
        while True:
            for i in range(self.consumed, len(self.lines)):
                if r.search(self.lines[i]):
                    self.consumed = i + 1
                    return self.lines[i]
            if time.time() >= end: break
            self.pump(0.2)
        raise SystemExit(f"EXPECT FAILED: {rx}")

def main(path):
    c = Conn()
    c.pump(1)
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith("#"): continue
        cmd, _, arg = line.partition(" ")
        if cmd == "send": c.send(arg); c.pump(0.3)
        elif cmd == "wait": c.pump(float(arg))
        elif cmd == "mash":
            # mash <buttons hex> <count> [hold ms] [gap s]: repeated presses, e.g. to page through dialogue
            p = arg.split()
            hold = p[2] if len(p) > 2 else "400"
            gap = float(p[3]) if len(p) > 3 else 0.9
            for _ in range(int(p[1])):
                c.send(f"DBG input {p[0]} {hold}"); c.pump(gap)
        elif cmd == "shot":
            subprocess.run([sys.executable, "-I", os.path.join(HERE, "emu.py"), "shot", arg])
        elif cmd == "expect":
            parts = arg.rsplit(" ", 1)
            secs = float(parts[1]) if len(parts) == 2 and parts[1].replace(".", "").isdigit() else 30
            rx = parts[0] if len(parts) == 2 and parts[1].replace(".", "").isdigit() else arg
            print("matched:", c.expect(rx, secs))
    print("SCRIPT OK")

if __name__ == "__main__":
    main(sys.argv[1])
