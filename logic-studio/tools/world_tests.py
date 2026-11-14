"""The world's tests that Logic Studio's Tests panel runs, which the manifest names as `tests`.

python world_tests.py                              the tests' names, as a JSON list
python world_tests.py <name> [--logic <logic.yml>]  runs one in vendor/Archipelago and prints {"status", "summary", "failed"}
                                                    as JSON; the file stands in for the world's logic.yml (logic_override)
Any Python starts it: it runs itself in .venv-ap.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parents[1]
AP = ROOT / "vendor" / "Archipelago"
TESTS = ("test_logic.py", "test_logic_data.py", "test_key_logic.py")


def run(name, logic_file=None):
    if name not in TESTS:
        return {"status": "failed", "summary": f"No test {name}", "failed": []}
    if not (AP / "worlds" / "las").exists():
        return {"status": "failed", "summary": "vendor/Archipelago has no worlds/las: run bash scripts/worktree.sh setup", "failed": []}
    env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}
    override = []
    if logic_file:
        if not Path(logic_file).is_file():
            return {"status": "failed", "summary": f"No logic file {logic_file}", "failed": []}
        env.update({"LASAP_LOGIC_FILE": str(Path(logic_file).resolve()), "PYTHONPATH": os.pathsep.join(filter(None, (str(TOOLS), env.get("PYTHONPATH"))))})
        override = ["-p", "logic_override"]
    done = subprocess.run([sys.executable, "-B", "-m", "pytest", *override, f"worlds/las/test/{name}", "-q", "-p", "no:cacheprovider", "--no-header", "--tb=line", "--color=no"],
                          cwd=AP, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)
    lines = done.stdout.strip().splitlines()
    summary = lines[-1].strip("= ") if lines else (done.stderr.strip().splitlines() or ["No output"])[-1]
    failed = []
    for line in lines:
        sub = re.match(r"SUBFAILED(\(.*\)) \S*?test/(\S+)", line)  # a subtest: the test, then what it was run with
        if sub:
            failed.append(f"{sub[2]} {sub[1]}"[:300])
        elif line.startswith(("FAILED", "ERROR")):
            failed.append(re.sub(r"^(FAILED|ERROR) \S*?test/", "", line)[:300])
    return {"status": "passed" if done.returncode == 0 else "failed", "summary": summary, "failed": failed[:80]}


if __name__ == "__main__":
    import venv_ap
    venv_ap.relaunch()
    args = sys.argv[1:]
    logic = args[args.index("--logic") + 1] if "--logic" in args else None
    names = [a for a in args if a != "--logic" and a != logic]
    print(json.dumps(run(names[0], logic) if names else list(TESTS)))
