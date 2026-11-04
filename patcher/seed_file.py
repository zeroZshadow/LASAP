"""The seed file next to config.ini (DESIGN.md, Seed file): the per-seed tables the module reads, a `[<section>]` line
before each section's lines (AP::SEED_PATH; the module's config/seed_file.cpp). A section without lines is left out,
which the module reads as an empty one: the map tracker, the hints and the shuffles are off without theirs.
"""
from pathlib import Path

FILE_NAME = "ap_seed.txt"


def write(config_dir: Path, sections: dict) -> None:
    """`sections`: name -> lines, in the file's order."""
    text = []
    for name, lines in sections.items():
        if not lines:
            continue
        assert not any(line.startswith("[") or "\n" in line for line in lines), name
        text.append(f"[{name}]\n" + "".join(line + "\n" for line in lines))
    (config_dir / FILE_NAME).write_text("".join(text), encoding="utf-8", newline="\n")


def read(config_dir: Path) -> dict:
    """name -> lines; a section the file leaves out is not there."""
    return read_text((config_dir / FILE_NAME).read_text(encoding="utf-8"))


def read_text(text: str) -> dict:
    sections, lines = {}, None
    for line in text.splitlines():
        if line.startswith("[") and line.endswith("]"):
            lines = sections.setdefault(line[1:-1], [])
        elif line and lines is not None:
            lines.append(line)
    return sections
