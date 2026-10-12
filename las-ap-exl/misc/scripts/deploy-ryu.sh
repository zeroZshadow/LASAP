#!/bin/bash
set -e

# Verify the path is set.
if [ -z "${RYU_PATH}" ]; then
    echo "RYU_PATH appears to not be set! Check your exlaunch.sh?"
fi

# Setup the path to the game's mods folder.
export MODS_PATH=${RYU_PATH}/sdcard/${SD_OUT}

# Ensure directory exists.
mkdir -p ${MODS_PATH};

# Copy over files.
cp ${OUT}/* ${MODS_PATH}

# Setup the path to the mod config file
export CONFIG_PATH=${RYU_PATH}/sdcard/config/lasr-exl
mkdir -p ${CONFIG_PATH};
cp ${CONFIG_OUT} ${CONFIG_PATH}