"""Another logic.yml in place of the world's: the Simulator's draft and the Tests panel's project export.

As a pytest plugin (`-p logic_override`) it reads the file from LASAP_LOGIC_FILE; anything that loads the world after it
gets that file's logic. The world's data on disk stays as it is.
"""
import os
import pkgutil
from pathlib import Path

ENV = "LASAP_LOGIC_FILE"


def use(logic_file):
    """The world's data/logic.yml reads as this file from now on."""
    text = Path(logic_file).read_bytes()
    real_get_data = pkgutil.get_data

    def get_data(package, resource):
        if resource == "data/logic.yml" and package.endswith("las.lasr"):
            return text
        return real_get_data(package, resource)

    pkgutil.get_data = get_data


if os.environ.get(ENV):
    use(os.environ[ENV])
