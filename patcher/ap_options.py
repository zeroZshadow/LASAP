"""The options the RomFS holds both ways (DESIGN.md, Options): the mod is the same for every seed, and config.ini's
[options] section (OPTIONS) picks a way at runtime. LASR runs with LASR_SETTINGS; modbuild/ap_option_edits.py makes
their edits itself, each behind an option flag the module answers."""

FLAG_PREFIX = "APOpt."  # AP::Options::Flag
IF_PREFIX = "APIf|"
# config.ini [options] key -> whether a patch's settings turn the option on
OPTIONS = {
    "open_mabe": lambda s: s.get("Open Mabe", False),
    "classic_d2": lambda s: s.get("Classic D2", False),
    "completed_bridge": lambda s: s.get("Completed Bridge", False),
    "randomize_enemies": lambda s: s.get("Randomize Enemies", False),
    "fast_fishing": lambda s: s.get("Fast Fishing", False),
    "shuffled_bombs": lambda s: s.get("Shuffled Bombs", False),
    "shuffled_powder": lambda s: s.get("Shuffled Powder", False),
    "song_cutscenes": lambda s: s.get("Song Cutscenes", False),  # on skips them
    "owl_overworld": lambda s: s.get("Owl Gifts", "None") in ("Overworld", "All"),
    "owl_dungeons": lambda s: s.get("Owl Gifts", "None") in ("Dungeons", "All"),
    "ammo_ghost": lambda s: s.get("Ammo Ghost", True),  # the option's default, also for patches without it
    "bouncy_items": lambda s: s.get("Bouncy Items", False),
}
# The values LASR runs with in the asset build: the mod's own, whatever the seed's
LASR_SETTINGS = {
    "Open Mabe": False, "Classic D2": False, "Completed Bridge": False, "Randomize Enemies": False,
    "Fast Fishing": False, "Shuffled Bombs": False, "Shuffled Powder": False, "Song Cutscenes": False,
    "Owl Gifts": "None", "Traps": "Many", "Music": "Vanilla", "Randomize Text": False,
    "Randomize Environments": False,
}


def config_lines(settings: dict) -> str:
    return "\n[options]\n" + "".join(f"{key} = {bool(on(settings))}\n" for key, on in OPTIONS.items())
