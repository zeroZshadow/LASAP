"""The options the RomFS holds both ways (DESIGN.md, Options): the mod is the same for every seed, and config.ini's
[options] section (OPTIONS) picks a way at runtime. The asset build reads no setting; modbuild/ap_option_edits.py makes
the options' edits, each behind an option flag the module answers."""

FLAG_PREFIX = "APOpt."  # AP::Options::Flag
IF_PREFIX = "APIf|"
# config.ini [options] key -> the setting that turns the option on
SETTINGS = {"open_mabe": "Open Mabe", "classic_d2": "Classic D2", "completed_bridge": "Completed Bridge",
            "randomize_enemies": "Randomize Enemies", "fast_fishing": "Fast Fishing",
            "shuffled_bombs": "Shuffled Bombs", "shuffled_powder": "Shuffled Powder",
            "song_cutscenes": "Song Cutscenes",  # on skips them
            "ammo_ghost": "Ammo Ghost", "bouncy_items": "Bouncy Items"}
# config.ini [options] key -> the Owl Gifts values that turn the option on
OWL_GIFTS = {"owl_overworld": ("Overworld", "All"), "owl_dungeons": ("Dungeons", "All")}
OPTIONS = (*SETTINGS, *OWL_GIFTS)


def config(settings: dict) -> dict:
    return {**{key: settings[setting] for key, setting in SETTINGS.items()},
            **{key: settings["Owl Gifts"] in gifts for key, gifts in OWL_GIFTS.items()}}
