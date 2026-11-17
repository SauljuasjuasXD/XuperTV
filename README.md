# XuperTV

Cliente homebrew para Nintendo Switch, escrito en C++17 con libnx, SDL2 y libmpv.
Versión **0.14.1**.

> **Basado en [XuperSwitch / Xuper TV de Jona Ruacho](https://github.com/jonaruacho/Xuper-TV-by-JonaRuacho)**,
> publicado bajo licencia MIT. Todo el crédito del cliente original es suyo; este
> repositorio es una versión modificada y conserva su [LICENSE](LICENSE).

No es una aplicación oficial de Nintendo ni de ningún proveedor. No incluye
cuentas, suscripciones, APK ni contenido: cada persona usa su propia configuración.

## Qué cambia respecto al original

- Nombre e imagen **XuperTV**: logo propio, paleta oscura con rojo y tipografía Inter.
- Interfaz de estilo streaming (menú lateral, filas y portadas).
- **Actualización automática con ZR** desde las releases de este repositorio,
  verificando el SHA256 antes de instalar.
- Opciones de rendimiento de mpv y reparto de hilos entre los núcleos de la consola.
- Subtítulos: botón −, normalización a UTF-8 y recuerdo del idioma.

Estos cambios se han probado en PC con datos sintéticos; **falta validarlos en una
consola real**.

## Instalar en la SD

Necesitas una Switch con custom firmware y el menú de homebrew (hbmenu).

1. Descarga `XuperTV-para-compartir.zip` desde
   [Releases](https://github.com/SauljuasjuasXD/XuperTV/releases/latest).
2. Copia la carpeta `switch` del zip a la raíz de la SD (combina o reemplaza si pregunta).
   El ejecutable queda en `sdmc:/switch/xuperswitch/xuperswitch.nro`.
3. Abre hbmenu **manteniendo R sobre un juego** (modo aplicación), no desde el
   álbum: así la app dispone de memoria suficiente para reproducir vídeo.
4. Ejecuta XuperTV.

La app necesita tu propio `service.json` en `sdmc:/switch/xuperswitch/`;
`service.template.json` sirve de guía. No borres `device_state.json` al actualizar
y no compartas ninguno de esos dos archivos.

### Actualizar

Pulsa **ZR** dentro de la app: busca la última release de este repositorio y, si es
más nueva, pulsa ZR otra vez para instalarla. Para usar otro fork, crea
`sdmc:/switch/xuperswitch/update.json` con `{"repo":"usuario/nombre"}`.

## Compilar

Hace falta el toolchain de Switch de **devkitPro** (devkitA64 + libnx) sobre **MSYS2**
en Windows, o devkitPro pacman en Linux/macOS:

- devkitPro: <https://devkitpro.org/wiki/Getting_Started>
- MSYS2: <https://www.msys2.org/>

Las dependencias (SDL2, SDL2_ttf, libmpv, FFmpeg, cURL, mbedTLS, libpng, libjpeg,
libwebp para Switch) y las versiones usadas están en [src/BUILD.md](src/BUILD.md)
y [src/DEPENDENCIAS.md](src/DEPENDENCIAS.md). `setup-devkitpro.sh` las instala en MSYS2.

Desde la terminal MSYS2:

```sh
./setup-devkitpro.sh      # una sola vez
./build-switch.sh clean   # genera src/xuperswitch.nro
./package.sh              # arma salida/switch/xuperswitch/ lista para la SD
```

O directamente:

```sh
cd src && DEVKITPRO=/opt/devkitpro make -j4
```

Para que ZR apunte a tu propio fork: `make UPDATE_REPO=usuario/nombre`.

### Publicar una release compatible con ZR

El actualizador exige, en la misma release:

- `SHA256SUMS.txt`, y
- `xuperswitch.nro` suelto **o** un paquete cuyo nombre termine en `_Switch.zip`
  (con el NRO en `switch/xuperswitch/xuperswitch.nro`).

El tag debe ser `vX.Y.Z` sin sufijos (por ejemplo `v0.14.2`) y la release no
puede ser borrador ni prerelease.

## Estructura

- `src/` — código del cliente, pruebas, recursos y empaquetado. El README del
  proyecto original está en [src/README.md](src/README.md).
- `host.sh`, `shots.sh` — compilar y probar la interfaz en PC.
- `build-switch.sh`, `package.sh`, `setup-devkitpro.sh` — compilación para Switch.

## Licencia y créditos

Código bajo **MIT**, © 2026 Jona Ruacho — ver [LICENSE](LICENSE). Las modificaciones
de XuperTV se publican bajo la misma licencia. Las dependencias mantienen las suyas:
`src/licenses/`, `src/LICENSE.nlohmann-json`, `src/COPYRIGHT.mpv` y
`src/assets/Inter-LICENSE.txt`.
