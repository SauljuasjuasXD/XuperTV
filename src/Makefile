TARGET := xuperswitch
APP_TITLE := XuperTV
APP_AUTHOR := Jona Ruacho
APP_VERSION := 0.14.1
# Autoactualizacion (ZR): repositorio de XuperTV. Para otro fork: make UPDATE_REPO=usuario/nombre,
# o en la SD switch/xuperswitch/update.json: {"repo":"usuario/nombre"}. La release debe traer
# SHA256SUMS.txt y xuperswitch.nro o un paquete *_Switch.zip.
UPDATE_REPO ?= SauljuasjuasXD/XuperTV

ifeq ($(strip $(DEVKITPRO)),)
$(error Instala devkitPro y abre su terminal MSYS2; falta DEVKITPRO)
endif
include $(DEVKITPRO)/libnx/switch_rules

PKG_CONFIG := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
PKGS := mpv sdl2 SDL2_ttf libcurl
PKG_OK := $(shell $(PKG_CONFIG) --exists $(PKGS) 2>/dev/null && echo yes)
ifneq ($(PKG_OK),yes)
$(error Faltan SDL2 o libmpv para Switch; consulta BUILD.md. No sirven bibliotecas de Windows/Linux)
endif
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -fno-rtti -fno-exceptions $(ARCH) \
    -D__SWITCH__ -DXS_VERSION=\"$(APP_VERSION)\" -DXS_UPDATE_REPO=\"$(UPDATE_REPO)\" -Iinclude -Iinclude/vendor -I$(DEVKITPRO)/libnx/include \
    $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDFLAGS := $(ARCH) -specs=$(DEVKITPRO)/libnx/switch.specs \
    -L$(DEVKITPRO)/libnx/lib -Wl,-Map,$(TARGET).map -Wl,--wrap=threadCreate
LIBS := $(shell $(PKG_CONFIG) --static --libs $(PKGS)) -lpng16 -ljpeg -lwebp -lz -lnx
OBJECTS := build/main.o build/catalog.o build/player.o build/codec.o build/ranger_codec.o build/service.o build/view.o build/artwork.o build/trace.o build/updater.o build/zip_extract.o build/switch_threads.o
NROFLAGS := --nacp=$(TARGET).nacp --icon=assets/icon.jpg
.DEFAULT_GOAL := all
.PHONY: all clean
all: $(TARGET).nro
$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
$(TARGET).nacp: Makefile
$(TARGET).elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)
build/%.o: source/%.cpp include/catalog.hpp include/player.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/view.o: $(wildcard assets/*.ttf assets/*.rgba)
clean:
	rm -rf build $(TARGET).nro $(TARGET).nacp $(TARGET).elf $(TARGET).map
-include $(OBJECTS:.o=.d)
