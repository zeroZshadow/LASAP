"""A new save's name starts as the slot name.

The name entry of a new file shows `UI:NameEntry_Default` ("Link") until the player renames it, and OK keeps it. The
module hands out the slot name there, as far as the game takes it (`save_name`), from config.ini ([archipelago]
save_name)."""
DEFAULT = "Link"
# The game's keyboard (nn::swkbd, UserName preset) takes up to 8 characters and rejects @, % and \; a name of spaces
# only is refused by the game's own check.
MAX_LENGTH = 8
# Brackets would start a tag in the message text.
REFUSED = set("@%\\[]")


def allowed(c: str) -> bool:
    """Printable ASCII and the Latin-1 letters, which the game's fonts have for its European languages."""
    if c in REFUSED:
        return False
    return " " <= c <= "~" or ("À" <= c <= "ÿ" and c not in "×÷")


def save_name(slot_name: str) -> str:
    """The slot name without the characters the game cannot take, runs of spaces as one, cut to MAX_LENGTH; "Link"
    when nothing is left."""
    name = " ".join("".join(c for c in slot_name if allowed(c)).split())
    name = name[:MAX_LENGTH].rstrip()
    return name or DEFAULT
