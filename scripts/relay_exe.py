"""Entry point of LASGhostRelay.exe (scripts/build_relay.py): the ghost relay (apworld/las/ghost_relay.py) as one
program that needs no Python.

Started without arguments, as by a double-click, it has the router forward its port (--forward), says which address
players enter, and keeps the window open after an error so its message can be read. With arguments it is
ghost_relay.py: LASGhostRelay.exe --port 43391, --help, ...
"""
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "apworld" / "las"))  # from source
import ghost_relay  # noqa: E402


def lan_address() -> str:
    """This computer's address in the local network (no packet is sent)."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("192.0.2.1", 9))
            return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def run() -> None:
    double_clicked = len(sys.argv) == 1
    if double_clicked:
        sys.argv.append("--forward")
        port = ghost_relay.DEFAULT_PORT
        print("Ghost relay for Link's Awakening (Switch) Archipelago. Close this window to stop it.")
        print(f"Players on this network enter {lan_address()}:{port} as the ghost relay; players elsewhere enter the "
              "address the relay names below once the router forwards the port.\n")
    try:
        ghost_relay.main()
    except Exception as error:  # e.g. the port is in use
        print(f"\nThe ghost relay stopped: {error}")
        if double_clicked:
            input("Press Enter to close this window.")
        sys.exit(1)


if __name__ == "__main__":
    run()
