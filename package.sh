#!/bin/bash
# Arma xuperswitch-src/salida/switch/xuperswitch/ lista para copiar a la SD.
# No incluye service.json, device_state.json ni otros archivos personales.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"; OUT="$ROOT/salida/switch/xuperswitch"
mkdir -p "$OUT"
cp "$ROOT/src/xuperswitch.nro" "$OUT/"
cp "$ROOT"/src/packaging/runtime/switch/xuperswitch/{cacert.pem,catalog.m3u,font.ttf,service.template.json,test.mp4} "$OUT/"
head -c 20 "$OUT/xuperswitch.nro" | tail -c 4; echo
# Comprobacion: las llamadas a threadCreate deben pasar por el reparto de nucleos.
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-objdump -d "$ROOT/src/xuperswitch.elf" --no-show-raw-insn \
  | grep -oE 'bl\s+[0-9a-f]+ <(__wrap_threadCreate|threadCreate)>' | awk '{print $NF}' | sort | uniq -c
(cd "$ROOT/salida" && sha256sum switch/xuperswitch/xuperswitch.nro > SHA256SUMS.txt && cat SHA256SUMS.txt)
ls -la "$OUT"
