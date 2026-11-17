#!/bin/bash
# Regenera src/assets (fuentes Inter, logo e icono). Entorno: MSYS2 UCRT64.
set -e
cd "$(dirname "$0")"
python -I make_logo.py D:/xuper-tools/inter/Inter-4.1.zip src/assets
ls -l src/assets
