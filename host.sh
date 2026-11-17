#!/bin/bash
# Compila la app para PC (MSYS2 UCRT64). mbedTLS 3.x renombro las funciones *_ret de 2.x.
set -e
cd "$(dirname "$0")/src"
SH='-Dmbedtls_md5_ret=mbedtls_md5 -Dmbedtls_sha256_starts_ret=mbedtls_sha256_starts -Dmbedtls_sha256_update_ret=mbedtls_sha256_update -Dmbedtls_sha256_finish_ret=mbedtls_sha256_finish'
FLAGS='-std=c++17 -O2 -Wall -Wextra -Iinclude -Iinclude/vendor -include ../host_win.h'
case "$1" in
  syntax) g++ $FLAGS $SH -Werror -fsyntax-only source/updater.cpp && echo UPDATER_SYNTAX_OK ;;
  player) make -f Makefile.host player CXXFLAGS="$FLAGS" MBEDTLS_CFLAGS="$SH" ;;
  ui) # Capturas de la interfaz con catalogo sintetico -> src/work/ui014/*.png
      mkdir -p build-host
      g++ $FLAGS tests/ui_artwork_probe.cpp source/view.cpp source/artwork.cpp source/catalog.cpp source/trace.cpp \
          $(pkg-config --cflags --libs sdl2 SDL2_ttf) -lcurl -lpng16 -ljpeg -lwebp -pthread -o build-host/ui_probe
      ../shots.sh "${2:-nueva}" ;;
  playerui) # Capturas de la interfaz del reproductor -> capturas/<nombre>/player_NN.png
      mkdir -p build-host
      g++ $FLAGS $SH -DXUPERSWITCH_TEST tests/player_ui_probe.cpp source/player.cpp source/view.cpp source/artwork.cpp source/catalog.cpp source/trace.cpp \
          $(pkg-config --cflags --libs sdl2 SDL2_ttf mpv) -lmbedcrypto -lcurl -lpng16 -ljpeg -lwebp -pthread -o build-host/player_ui_probe
      OUT="$(cd .. && pwd)/capturas/${2:-nueva}"; mkdir -p "$OUT"
      rm -rf /tmp/pui; mkdir -p /tmp/pui; cp packaging/runtime/switch/xuperswitch/{font.ttf,test.mp4} /tmp/pui/
      BIN="$PWD/build-host/player_ui_probe"; cd /tmp/pui
      (exec python -m http.server 8767 --bind 127.0.0.1 >/dev/null 2>&1) & SERVER=$!
      sleep 2; XS_PLAYER_SHOTS=player SDL_VIDEODRIVER=dummy timeout 60 "$BIN" http://127.0.0.1:8767/test.mp4; echo "exit=$?"; kill $SERVER
      python -c "
import glob,sys
from PIL import Image
for f in sorted(glob.glob('player_*.bmp')): Image.open(f).save(sys.argv[1]+'/'+f[:-4]+'.png')
print(len(glob.glob('player_*.bmp')),'capturas')" "$OUT" ;;
  zip) # Prueba el extractor con un ZIP generado y, si existe, con el paquete oficial.
      mkdir -p build-host; g++ $FLAGS -Werror tests/zip_extract_test.cpp source/zip_extract.cpp -lz -o build-host/zip_extract_test
      BIN="$PWD/build-host/zip_extract_test"; rm -rf /tmp/ziptest; mkdir -p /tmp/ziptest; cd /tmp/ziptest
      python -c "
import zipfile,os
data=os.urandom(70000)+b'XuperSwitch '*40000+os.urandom(5000)
open('expected.bin','wb').write(data)
z=zipfile.ZipFile('t.zip','w')
z.writestr('LEEME.txt','hola',zipfile.ZIP_DEFLATED)
z.writestr('switch/xuperswitch/xuperswitch.nro',data,zipfile.ZIP_DEFLATED)
z.writestr('stored.txt','sin comprimir\n',zipfile.ZIP_STORED)
z.close()"
      "$BIN" t.zip switch/xuperswitch/xuperswitch.nro expected.bin
      OFFICIAL=D:/xuper-tools/Xuper_Switch_0.14.1.zip
      if [ -f "$OFFICIAL" ]; then
        python -c "
import zipfile;z=zipfile.ZipFile('$OFFICIAL');open('official.nro','wb').write(z.read('switch/xuperswitch/xuperswitch.nro'))
z2=zipfile.ZipFile('o.zip','w');[z2.writestr(i,z.read(i.filename)) for i in z.infolist()];z2.writestr('stored.txt','sin comprimir\n',zipfile.ZIP_STORED);z2.close()"
        "$BIN" o.zip switch/xuperswitch/xuperswitch.nro official.nro && echo "paquete oficial: NRO extraido identico"
      fi ;;
  smoke) # Reproduce test.mp4 dos veces por HTTP local, sin audio y sin ventana.
      mkdir -p build-host
      g++ $FLAGS $SH -DXUPERSWITCH_TEST tests/playback_smoke.cpp source/player.cpp source/view.cpp source/artwork.cpp source/catalog.cpp source/trace.cpp source/ranger_codec.cpp \
          $(pkg-config --cflags --libs sdl2 SDL2_ttf mpv) -lmbedcrypto -lcurl -lpng16 -ljpeg -lwebp -pthread -o build-host/playback_smoke
      rm -rf /tmp/smoke; mkdir -p /tmp/smoke; cp packaging/runtime/switch/xuperswitch/{font.ttf,test.mp4} /tmp/smoke/
      BIN="$PWD/build-host/playback_smoke"; cd /tmp/smoke
      (exec python -m http.server 8766 --bind 127.0.0.1 >/dev/null 2>&1) & SERVER=$!
      sleep 2; SDL_VIDEODRIVER=dummy timeout 120 "$BIN" http://127.0.0.1:8766/test.mp4; echo "smoke exit=$?"; kill $SERVER ;;
  *) echo "uso: host.sh syntax|player|ui|smoke"; exit 1 ;;
esac
