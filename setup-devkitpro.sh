#!/bin/bash
# Instala devkitPro (devkitA64 + libnx + portlibs de Switch) dentro de MSYS2 y los
# paquetes de medios de wiliwili que indica BUILD.md, verificando sus SHA-256.
set -e
KEY=BC26F752D25B92CE272E0F44F7FD5492264BB9D0
if ! grep -q '^\[dkp-libs\]' /etc/pacman.conf; then
  pacman-key --recv "$KEY" --keyserver keyserver.ubuntu.com
  pacman-key --lsign "$KEY"
  pacman -U --noconfirm https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst
  pacman-key --populate devkitpro
  printf '\n[dkp-libs]\nServer = https://pkg.devkitpro.org/packages\n\n[dkp-windows]\nServer = https://pkg.devkitpro.org/packages/windows/$arch/\n' >> /etc/pacman.conf
fi
pacman -Sy --noconfirm
pacman -S --noconfirm --needed switch-dev switch-sdl2 switch-sdl2_ttf switch-curl switch-mbedtls switch-zlib \
  switch-libpng switch-libjpeg-turbo switch-libwebp switch-mesa switch-freetype switch-harfbuzz switch-libass \
  switch-libfribidi switch-dav1d switch-liblua51 switch-pkg-config
mkdir -p /tmp/wili && cd /tmp/wili
BASE=https://github.com/xfangfang/wiliwili/releases/download/v0.1.0
fetch(){ [ -f "$1" ] || curl -fsSL -o "$1" "$BASE/$1"; echo "$2  $1" | sha256sum -c -; }
fetch switch-ffmpeg-7.1-1-any.pkg.tar.zst 993e8ce950cf43040f1c213daf66c6dd4cd7e89d731ea6239856a76b40d65fa6
fetch switch-libmpv-0.36.0-3-any.pkg.tar.zst 672aa214a6cb811db7c39bdeebe4462e3a2eed47c53eac8b562b7ccd8fc3c859
pacman -U --noconfirm --needed switch-ffmpeg-7.1-1-any.pkg.tar.zst switch-libmpv-0.36.0-3-any.pkg.tar.zst
echo SETUP_OK
