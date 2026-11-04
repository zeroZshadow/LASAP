"""In-game hints (DESIGN.md, In-game hints): owl statues and library books say the patch's `hints`.

In every patch each source shows its own message (modbuild/ap_hint_edits.py), Hint:APHint<n> for owl statues and System:APHint<n> for
books (n: the source's place in SOURCES): a copy of the text it replaces, with its window style. The module shows the
seed's hint in it instead and sets the event flag APHint<n> (FLAGS), from the seed file's hints (`lines`), which also names the
location a server hint makes, and names a book with a hint in its question.

Owl statues that share their text get their own: the Tail Cave statue in room 04B (which shares 05F's entry) and the
three Color Dungeon statues get entries of their own, as LASR's Owl Gifts give them (owls.py), each with its own copy
of the statue's events. With Owl Gifts on, the gift follows the hint."""
FLAG_PREFIX = "APHint"  # flag and message label prefix; AP::HINT_PREFIX
LINE_WIDTH = 42  # characters per line
PAGE_LINES = 3  # lines per text box

FIELD_FLOW, DUNGEON_FLOW, BOOK_FLOW = "FieldOwlStatue.bfevfl", "DungeonOwlStatue.bfevfl", "Book.bfevfl"
# Overworld owl statues: their message (Hint.msbt) in FieldOwlStatue.bfevfl
FIELD_OWLS = {
    "owl-statue-below-D8": "WestTalTalStoneOwl",
    "owl-statue-pothole": "PotholeStoneOwl",
    "owl-statue-above-cave": "UkukuStoneOwl",
    "owl-statue-moblin-cave": "TalTalHeightStoneOwl",
    "owl-statue-south-bay": "MarthaStoneOwl",
    "owl-statue-desert": "YarnaStoneOwl",
    "owl-statue-maze": "FaceStoneOwl",
    "owl-statue-taltal-east": "EastTalTalStoneOwl",
    "owl-statue-rapids": "RapidsRideStoneOwl",
}
# Dungeon owl statues: their message in DungeonOwlStatue.bfevfl
DUNGEON_OWLS = {
    "D1-owl-statue-spinies": "Lv1StoneOwl1",
    "D1-owl-statue-3-of-a-kind": "Lv1StoneOwl1",
    "D1-owl-statue-long-hallway": "Lv1StoneOwl2",
    "D2-owl-statue-first-switch": "Lv2StoneOwl1",
    "D2-owl-statue-push-puzzle": "Lv2StoneOwl2",
    "D2-owl-statue-past-hinox": "Lv2StoneOwl3",
    "D3-owl-statue-basement-north": "Lv3StoneOwl1",
    "D3-owl-statue-arrow": "Lv3StoneOwl2",
    "D3-owl-statue-northwest": "Lv3StoneOwl3",
    "D4-owl-statue": "Lv4StoneOwl",
    "D5-owl-statue-triple-stalfos": "Lv5StoneOwl1",
    "D5-owl-statue-before-slime-eel": "Lv5StoneOwl2",
    "D6-owl-statue-ledge": "Lv6StoneOwl1",
    "D6-owl-statue-southeast": "Lv6StoneOwl2",
    "D6-owl-statue-canal": "Lv6StoneOwl3",
    "D7-owl-statue-ball": "Lv7StoneOwl1",
    "D7-owl-statue-kirbys": "Lv7StoneOwl2",
    "D7-owl-statue-hookshot-chest": "Lv7StoneOwl3",
    "D8-owl-statue-above-smasher": "Lv8StoneOwl1",
    "D8-owl-statue-below-gibdos": "Lv8StoneOwl2",
    "D8-owl-statue-eye-statue": "Lv8StoneOwl3",
    "D0-owl-statue-nine-switches": "ColorDungeonStoneOwl",
    "D0-owl-statue-first-switches": "ColorDungeonStoneOwl",
    "D0-owl-statue-before-mini-boss": "ColorDungeonStoneOwl",
}
# Statues whose entry point other statues share: (their own entry, their room, the shared entry). The room's statue
# is pointed at its own entry, as LASR does for Owl Gifts.
OWN_ENTRIES = {
    "D1-owl-statue-spinies": ("examine_Tail04B", "Lv01TailCave_04B", "examine_Tail04B05F"),
    "D1-owl-statue-3-of-a-kind": ("examine_Tail04B05F", None, None),
    "D0-owl-statue-nine-switches": ("examine_Color06C", "Lv10ClothesDungeon_06C", "examine_Color"),
    "D0-owl-statue-first-switches": ("examine_Color07D", "Lv10ClothesDungeon_07D", "examine_Color"),
    "D0-owl-statue-before-mini-boss": ("examine_Color05F", "Lv10ClothesDungeon_05F", "examine_Color"),
}
# Library books: their text (System.msbt) in Book.bfevfl. The map, the egg book and the Color Dungeon's book stay.
BOOKS = {
    "book-Blue4": "ReadShieldBook",
    "book-Green1": "ReadItemSelect",
    "book-Orange2": "ReadMapandMemo",
    "book-Red3": "ReadTurnAttack",
    "book-Blue2": "ReadWarpRule",
    "book-Red1": "ReadLiftBomb",
}
# The books' question before they are read (System.msbt): the module names a book with a hint "Koholint Lost and
# Found, Volume <n>" in its first line, numbered in BOOKS' order.
BOOK_PROMPTS = {
    "book-Blue4": "CheckShieldBook",
    "book-Green1": "CheckItemSelect",
    "book-Orange2": "CheckMapandMemo",
    "book-Red3": "CheckTurnAttack",
    "book-Blue2": "CheckWarpRule",
    "book-Red1": "CheckLiftBomb",
}
SOURCES = (*FIELD_OWLS, *DUNGEON_OWLS, *BOOKS)  # flag and label numbers, in this order
BEAK_ITEM_TYPE = 63  # Inventory.HasItem itemType of the dungeon's stone beak



def label(source: str) -> str:
    return f"{FLAG_PREFIX}{SOURCES.index(source)}"


FLAGS = [label(source) for source in SOURCES]  # set when the source's hint shows; added to GlobalFlags by modbuild/mod.py


def clean(text: str) -> str:
    """Names come from any game: no tags (square brackets), control characters or characters beyond the BMP."""
    text = text.replace("[", "(").replace("]", ")")
    return "".join(c if c.isprintable() and ord(c) <= 0xFFFF else "?" for c in " ".join(text.split()))


def wrap(text: str) -> list:
    """The text's pages, each a list of up to PAGE_LINES lines of up to LINE_WIDTH characters."""
    words = []
    for word in clean(text).split(" "):
        if word == "-" and words:  # a line never starts with the dash of "<region> - <spot>"
            words[-1] += " -"
        else:
            words.append(word)
    lines, line = [], ""
    for word in words:
        while len(word) > LINE_WIDTH:  # a name without spaces
            if line:
                lines.append(line)
                line = ""
            lines.append(word[:LINE_WIDTH])
            word = word[LINE_WIDTH:]
        if line and len(line) + 1 + len(word) > LINE_WIDTH:
            lines.append(line)
            line = word
        else:
            line = f"{line} {word}" if line else word
    if line:
        lines.append(line)
    return [lines[i:i + PAGE_LINES] for i in range(0, len(lines), PAGE_LINES)]


def lines(hints: dict) -> list:
    """The seed file's hints, per source with a hint: `B <n> <question label> <volume>` for a book,
    `L <n> <text>` per line and `P <n>` before each further page, `S <n> <slot> <location id>` for a server hint."""
    out = []
    for n, source in enumerate(SOURCES):
        hint = hints.get(source)
        if hint is None:
            continue
        if source in BOOK_PROMPTS:
            out.append(f"B {n} {BOOK_PROMPTS[source]} {list(BOOKS).index(source) + 1}")
        for page, text in enumerate(wrap(hint["text"])):
            if page:
                out.append(f"P {n}")
            out += [f"L {n} {line}" for line in text]
        if "location" in hint:
            out.append(f"S {n} {int(hint['player'])} {int(hint['location'])}")
    return out
