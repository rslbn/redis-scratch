#!/bin/bash

set -e

no_build=false
debug=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)
            no_build=true
            shift
            ;;
        --debug)
            debug=true
            shift
            ;;
        *)
            break
            ;;
    esac
done

# Compilation flags
CXXFLAGS=("-Wall")
if [[ "$debug" == true ]]; then
    CXXFLAGS+=("-g")
fi

# Check build condition
if [[ "$no_build" == true ]]; then
    if [[ ! -f ./client ]]; then
        echo "Error: ./client binary not found and --no-build was specified." >&2
        exit 1
    fi
else
    g++ "${CXXFLAGS[@]}" src/client.cpp src/utils.cpp -I include/ -o client
fi

# Execute client passing all remaining arguments
./client "$@"