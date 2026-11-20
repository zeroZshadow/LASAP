#!/bin/bash

# User configuration has moved to config.mk.

# Make arguments.
MAKE_ARGS="-j8 V=1"

# Determine the make verb from the user action.
if [ "$1" == "build" ]; then
    MAKE_VERB=""
elif [ "$1" == "clean" ]; then
    MAKE_VERB="clean"
elif [ "$1" == "make-npdm-json" ]; then
    MAKE_VERB="npdm-json"
else
    echo "Invalid arg. (build/clean/make-npdm-json)"
    exit 1
fi

make $MAKE_ARGS $MAKE_VERB
