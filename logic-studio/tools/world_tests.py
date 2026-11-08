"""The world's tests that Logic Studio's Tests panel runs, which the manifest names as `tests`.

python world_tests.py          the tests' names, as a JSON list
python world_tests.py <name>   runs one in vendor/Archipelago and prints {"status", "summary", "failed"} as JSON
Any Python starts it: it runs itself in .venv-ap.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AP = ROOT / "vendor" / "Archipelago"
TESTS = ("test_logic.py", "test_logic_data.py", "test_key_logic.py")


def run(name):
    if name not in TESTS:
        return {"status": "failed", "summary": f"No test {name}", "failed": []}
    if not (AP / "worlds" / "las").exists():
        return {"status": "failed", "summary": "vendor/Archipelago has no worlds/las: run bash scripts/worktree.sh setup", "failed": []}
    done = subprocess.run([sys.executable, "-B", "-m", "pytest", f"worlds/las/test/{name}", "-q", "-p", "no:cacheprovider", "--no-header", "--tb=line", "--color=no"],
                          cwd=AP, capture_output=True, text=True, encoding="utf-8", errors="replace", env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
    lines = done.stdout.strip().splitlines()
    summary = lines[-1].strip("= ") if lines else (done.stderr.strip().splitlines() or ["No output"])[-1]
    failed = [re.sub(r"^FAILED \S*?test/", "", line)[:300] for line in lines if line.startswith(("FAILED", "ERROR"))]
    return {"status": "passed" if done.returncode == 0 else "failed", "summary": summary, "failed": failed[:40]}


if __name__ == "__main__":
    import venv_ap
    venv_ap.relaunch()
    print(json.dumps(run(sys.argv[1]) if len(sys.argv) > 1 else list(TESTS)))
