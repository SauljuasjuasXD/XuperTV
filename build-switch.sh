#!/bin/bash
# Compila xuperswitch.nro (ejecutar en la terminal MSYS de D:\msys64).
# uso: build-switch.sh [clean] [UPDATE_REPO=usuario/fork]
set -e
cd "$(dirname "$0")/src"
export DEVKITPRO=/opt/devkitpro DEVKITA64=/opt/devkitpro/devkitA64
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
if [ "$1" = clean ]; then make clean; shift; fi
make -j4 "$@"
ls -la xuperswitch.nro
