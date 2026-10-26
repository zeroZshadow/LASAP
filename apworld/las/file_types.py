"""Opening .aplas files with a double-click (Windows). Archipelago's installer registers the patch types of its own
games; a custom world registers its own for the current user, as the installer does under HKCR."""
import os
import sys
from typing import Optional

PROG_ID = "Archipelago.LASPatch"
DESCRIPTION = "Archipelago Link's Awakening (Switch) Patch"


def launcher_command() -> Optional[str]:
    """The command that opens a file with the Archipelago Launcher, which hands .aplas files to this client."""
    import Utils
    if Utils.is_frozen():
        exe = os.path.join(os.path.dirname(sys.executable), "ArchipelagoLauncher.exe")
        return f'"{exe}" "%1"' if os.path.isfile(exe) else None
    launcher = Utils.local_path("Launcher.py")
    if not os.path.isfile(launcher):
        return None
    if os.environ.get("SKIP_REQUIREMENTS_UPDATE"):
        # a source checkout run without the requirements check keeps running without it
        code = ("import os,runpy,sys;os.environ['SKIP_REQUIREMENTS_UPDATE']='1';"
                f"os.chdir(r'{os.path.dirname(launcher)}');sys.path.insert(0,os.getcwd());"
                f"sys.argv=[r'{launcher}']+sys.argv[1:];runpy.run_path(sys.argv[0],run_name='__main__')")
        return f'"{_python()}" -c "{code}" "%1"'
    return f'"{_python()}" "{launcher}" "%1"'


def _python() -> str:
    """pythonw.exe when it is next to python.exe, so no console window opens."""
    pythonw = os.path.join(os.path.dirname(sys.executable), "pythonw.exe")
    return pythonw if os.path.isfile(pythonw) else sys.executable


def _icon() -> str:
    import Utils
    if Utils.is_frozen():
        return os.path.join(os.path.dirname(sys.executable), "ArchipelagoLauncher.exe") + ",0"
    return sys.executable + ",0"


def is_registered() -> bool:
    if sys.platform != "win32":
        return False
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Classes\.aplas") as key:
            if winreg.QueryValue(key, None) != PROG_ID:
                return False
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, rf"Software\Classes\{PROG_ID}\shell\open\command") as key:
            return winreg.QueryValue(key, None) == launcher_command()
    except OSError:
        return False


def other_program() -> bool:
    """Whether the user picked another program for .aplas files in Windows' "Open with", which wins over this."""
    if sys.platform != "win32":
        return False
    import winreg
    path = r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.aplas\UserChoice"
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
            return winreg.QueryValueEx(key, "ProgId")[0] != PROG_ID
    except OSError:
        return False


def register() -> str:
    """Registers .aplas for the current user. Returns an error message, or "" on success."""
    if sys.platform != "win32":
        return "Double-click opening can only be set up on Windows."
    command = launcher_command()
    if command is None:
        return "Could not find the Archipelago Launcher."
    import ctypes
    import winreg
    values = {
        r"Software\Classes\.aplas": PROG_ID,
        rf"Software\Classes\{PROG_ID}": DESCRIPTION,
        rf"Software\Classes\{PROG_ID}\DefaultIcon": _icon(),
        rf"Software\Classes\{PROG_ID}\shell\open\command": command,
    }
    try:
        for path, value in values.items():
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path) as key:
                winreg.SetValue(key, "", winreg.REG_SZ, value)
    except OSError as e:
        return f"Could not register .aplas files: {e}"
    ctypes.windll.shell32.SHChangeNotify(0x08000000, 0, None, None)  # SHCNE_ASSOCCHANGED
    return ""


def unregister() -> str:
    """Removes what register() wrote for the current user. Returns an error message, or "" on success."""
    if sys.platform != "win32":
        return ""
    import ctypes
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Classes\.aplas") as key:
            ours = winreg.QueryValue(key, None) == PROG_ID
    except OSError:
        ours = False
    try:
        _delete_tree(winreg.HKEY_CURRENT_USER, rf"Software\Classes\{PROG_ID}")
        if ours:  # only its default: other programs may be listed under it (OpenWithProgids)
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Classes\.aplas", 0, winreg.KEY_SET_VALUE) as key:
                winreg.DeleteValue(key, "")
    except OSError as e:
        return f"Could not remove the .aplas registration: {e}"
    ctypes.windll.shell32.SHChangeNotify(0x08000000, 0, None, None)  # SHCNE_ASSOCCHANGED
    return ""


def _delete_tree(root, path: str) -> None:
    """Deletes a registry key with its subkeys (DeleteKey removes only keys without subkeys); a missing key is
    fine."""
    import winreg
    try:
        with winreg.OpenKey(root, path) as key:
            subkeys = [winreg.EnumKey(key, i) for i in range(winreg.QueryInfoKey(key)[0])]
    except FileNotFoundError:
        return
    for name in subkeys:
        _delete_tree(root, rf"{path}\{name}")
    winreg.DeleteKey(root, path)
