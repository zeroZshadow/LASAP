"""Scripted session against the LAS AP module: python drive.py <script.txt>
Script lines: 'send <line>' | 'wait <secs>' | 'shot <png>' | 'expect <regex> [secs]' | '# comment'"""
import re, socket, subprocess, sys, time, os
HERE = os.path.dirname(os.path.abspath(__file__))

class Conn:
    def __init__(self, host="127.0.0.1", port=43384):
        deadline = time.time() + 60
        while True:
            try:
                self.s = socket.create_connection((host, port), timeout=2); break
            except OSError:
                if time.time() > deadline: raise
                time.sleep(1)
        self.s.settimeout(0.1); self.buf = b""; self.lines = []; self.last_ping = 0
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
        r = re.compile(rx); end = time.time() + secs; seen = len(self.lines)
        while time.time() < end:
            self.pump(0.2)
            for t in self.lines[seen:]:
                if r.search(t): return t
            seen = len(self.lines)
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
