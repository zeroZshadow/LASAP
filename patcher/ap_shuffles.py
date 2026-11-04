"""The player's shuffles, LASR's Music (Shuffled, Removed), Randomize Environments and Randomize Text, with a RomFS that
is the same for every seed: the seed file's shuffles say what each swaps, and the module swaps it as the game looks it up
(ap_shuffles.cpp; DESIGN.md, Options). LASR's own shuffles stay off (ap_options.LASR_SETTINGS).

Music: a track for each of LASR's tracks, or none. Environments: per environment one of its kind (top-down or side
view), as LASR picks per zone. Text: LASR's messages in a new order, the Talker names among themselves and the
dialogue across its files; a message that ends waiting for a button keeps doing so (LASR's fix of the moved text).
LASR's tracks and environments and the messages come from the mod's build (ap_assets.data; text_pools).
"""
from pathlib import Path

import ap_assets

TEXT_DIR = Path("regionUS", "USen", "message")  # the language the fixes are taken from: every language has the labels
DIALOGUE = ("Hint", "Npc", "Scenario", "SubEvent", "System", "Telephone")
WAIT, HOLD = "[1:4]", "[1:6]"
TAIL = len(WAIT) + len(HOLD)  # what _fix reads of a message: its end


def music_lines(setting: str, rng) -> list:
    tracks = ap_assets.data()["bgm_tracks"]
    if setting == "Shuffled":
        shuffled = list(tracks)
        rng.shuffle(shuffled)
        return [f"M {track} {to}" for track, to in zip(tracks, shuffled)]
    if setting == "Removed":
        return [f"M {track} -" for track in tracks]
    return []


def environment_lines(rng) -> list:
    return [f"E {name} {rng.choice(kind)}" for kind in ap_assets.data()["environments"] for name in kind]


def _valid(label: str, text: str) -> bool:
    """A message Randomize Text may move, as LASR's text randomizer has it: no item-gets, purchases, shops or
    choices."""
    return not any(word in label for word in ("Get", "Buy", "Shop")) and "[1:7]" not in text


def _fix(old: str, new: str) -> str:
    """How LASR's assignRandomizedText changes `new` shown in `old`'s place: + a wait added, - taken off, = kept."""
    text = new
    if old.endswith(WAIT):
        text = text if text.endswith(WAIT) else text + WAIT
    elif text.endswith(WAIT):
        text = text.removesuffix(WAIT)
    if old.endswith(HOLD) and not text.endswith(HOLD):
        text += WAIT
    return "=" if text == new else "+" if text == new + WAIT else "-"


def text_pools(texts) -> dict:
    """The messages Randomize Text shuffles, for the build (ap_assets.data): the Talker names and the dialogue, each
    `<file>:<label>` with its end. texts(name) gives a message file's texts by label."""
    return {"talkers": {f"Talker:{label}": text[-TAIL:] for label, text in texts("Talker").items()},
            "dialogue": {f"{name}:{label}": text[-TAIL:] for name in DIALOGUE for label, text in texts(name).items()
                         if _valid(label, text)}}


def text_lines(rng) -> list:
    pools = ap_assets.data()["texts"]
    lines = []
    for pool, fixed in ((pools["talkers"], False), (pools["dialogue"], True)):
        keys = list(pool)
        donors = list(keys)
        rng.shuffle(donors)
        lines += [f"T {key} {donor} {_fix(pool[key], pool[donor]) if fixed else '='}" for key, donor in zip(keys, donors)]
    return lines


def lines(settings: dict, rng) -> list:
    shuffled = music_lines(settings.get("Music", "Vanilla"), rng)
    if settings.get("Randomize Environments"):
        shuffled += environment_lines(rng)
    if settings.get("Randomize Text"):
        shuffled += text_lines(rng)
    return shuffled
