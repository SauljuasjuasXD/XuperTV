# Genera los recursos de marca de XuperTV y extrae las fuentes Inter (licencia OFL).
# uso: python -I make_logo.py <Inter-4.1.zip> <dir src/assets>
# Logo propio: dos cheurones enfrentados (rojo y blanco) que forman una X.
import sys, zipfile, os
from PIL import Image, ImageDraw

zip_path, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
z = zipfile.ZipFile(zip_path)
for name in ("Inter-Regular.ttf", "Inter-SemiBold.ttf", "InterDisplay-Bold.ttf"):
    data = z.read("extras/ttf/" + name)
    assert data[:4] in (b"\x00\x01\x00\x00", b"OTTO") and len(data) < 2_000_000, name
    open(os.path.join(out, name), "wb").write(data)
open(os.path.join(out, "Inter-LICENSE.txt"), "wb").write(z.read("LICENSE.txt"))

RED, WHITE = (229, 9, 20, 255), (255, 255, 255, 255)

def mark(size):
    s = 8                                   # supermuestreo
    n = size * s
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    t = int(n * 0.21)                       # grosor del trazo
    top, bottom, mid = n * 0.14, n * 0.86, n * 0.5
    def chevron(x_open, x_apex, color):
        pts = [(x_open, top), (x_apex, mid), (x_open, bottom)]
        d.line(pts, fill=color, width=t, joint="curve")
        for x, y in pts:
            d.ellipse((x - t / 2, y - t / 2, x + t / 2, y + t / 2), fill=color)
    chevron(n * 0.86, n * 0.56, WHITE)      # "<" a la derecha
    chevron(n * 0.14, n * 0.50, RED)        # ">" a la izquierda, por encima
    return im.resize((size, size), Image.LANCZOS)

# Se dibujan a la mitad exacta de su tamano (72 y 32 px): el filtro bilineal no deja dientes.
open(os.path.join(out, "logo.rgba"), "wb").write(mark(144).tobytes())
open(os.path.join(out, "logo_small.rgba"), "wb").write(mark(64).tobytes())
preview = Image.new("RGBA", (192, 192), (20, 20, 20, 255))
preview.alpha_composite(mark(144), (24, 24))
preview.save(os.path.join(out, "logo.png"))

icon = Image.new("RGBA", (256, 256), (20, 20, 20, 255))
m = mark(168)
icon.alpha_composite(m, (44, 44))
icon.convert("RGB").save(os.path.join(out, "icon.jpg"), quality=95)
print("fuentes, logo.rgba (144x144), logo_small.rgba (64x64) e icon.jpg generados en", out)
