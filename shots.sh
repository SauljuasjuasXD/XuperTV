#!/bin/bash
# Renderiza la interfaz con un catalogo sintetico y deja PNG en xuperswitch-src/capturas/<nombre>/
# uso: shots.sh <nombre>   (el arnes puede fallar al cerrar; las capturas ya quedan escritas)
P="$(cd "$(dirname "$0")/src" && pwd)"; OUT="$(cd "$(dirname "$0")" && pwd)/capturas/${1:-actual}"
rm -rf /tmp/uit; mkdir -p /tmp/uit/www "$OUT" && cp "$P/packaging/runtime/switch/xuperswitch/font.ttf" /tmp/uit/ && cd /tmp/uit
python - <<'EOF'
from PIL import Image,ImageDraw
def art(name,fmt,size,a,b):
    w,h=size;im=Image.new('RGB',size,a);d=ImageDraw.Draw(im)
    for y in range(h):
        t=y/h;d.line([(0,y),(w,y)],fill=tuple(int(int(a[i:i+2],16)*(1-t)+int(b[i:i+2],16)*t) for i in (1,3,5)))
    d.ellipse((w*.15,h*.2,w*.85,h*.62),fill=b);d.polygon([(0,h),(w*.5,h*.5),(w,h)],fill='#0b1220');im.save('www/'+name,fmt)
art('logo.png','PNG',(300,180),'#153e4f','#51c6b1')
art('poster.jpg','JPEG',(600,900),'#1b6376','#f2a65a')
art('poster.webp','WEBP',(600,900),'#593346','#e05263')
EOF
(cd www && exec python -m http.server 8765 --bind 127.0.0.1 >/dev/null 2>&1) &
SERVER=$!
sleep 2
SDL_VIDEODRIVER=dummy "$P/build-host/ui_probe" http://127.0.0.1:8765 >/dev/null 2>&1
kill $SERVER 2>/dev/null
python - "$OUT" <<'EOF'
import sys,glob,os
from PIL import Image
for f in sorted(glob.glob('*.bmp')):
    Image.open(f).save(os.path.join(sys.argv[1],f[:-4]+'.png'));print(f[:-4]+'.png')
EOF
