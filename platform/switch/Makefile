# Super3DX+ for Nintendo Switch (homebrew)
#
# Builds super3dx+nx.nro from the shared src/ and the SDL2 backend, using
# devkitPro's devkitA64 + libnx + switch-sdl2 (SDL2-for-Switch) + switch-mesa.
#
#   make -C platform/switch
#
# The generated tables and recompiled physics must already exist: run
#   python build.py data <your legally obtained dump folder>
# once on this machine. The converted game data is NOT built into the NRO;
# copy it beside the NRO on the SD card (see docs/SWITCH.md).
#
# Installed layout (like any other homebrew):
#   sdmc:/switch/super3dx+nx/super3dx+nx.nro
#   sdmc:/switch/super3dx+nx/data/...
#   sdmc:/switch/super3dx+nx/save/...   (created on first run)

.SUFFIXES:
.DEFAULT_GOAL := all

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Install devkitPro (https://devkitpro.org/wiki/Getting_Started/devkitPro) and build from its msys2 shell, or on Linux/macOS with DEVKITPRO exported.")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

# ----------------------------------------------------------------------------
# project metadata and sources
# ----------------------------------------------------------------------------
APP_TITLE	:=	Super3DX+NX
APP_AUTHOR	:=	Super3DX+ contributors
APP_VERSION	:=	1.0

TARGET	:=	super3dx+nx
BUILD	:=	build

# switch_rules' template discovers .c files only in these directories (not
# recursively): the shared game, the generated physics, the SDL2 backend and
# this wrapper. stb_vorbis.c is #included by bgm.c, so third_party/stb stays on
# the include path but is not compiled as its own translation unit.
SOURCES	:=	../../src ../../src/phys ../../platform/sdl2 ../../platform/switch
INCLUDES:=	../../src ../../src/phys ../../platform/sdl2 ../../platform/switch \
		../../third_party/stb ../../third_party/khronos

# ----------------------------------------------------------------------------
# devkitA64 / switch-sdl2 / switch-mesa
# ----------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ftls-model=local-exec

SDL2_CONFIG	?=	$(DEVKITPRO)/portlibs/switch/bin/sdl2-config
SDL_CFLAGS	:=	$(shell $(SDL2_CONFIG) --cflags 2>/dev/null)
SDL_LIBS	:=	$(shell $(SDL2_CONFIG) --libs 2>/dev/null)

# main_sdl.c provides SDL's entry point; main_switch.c provides libnx's real
# main(). SDL_MAIN_HANDLED stops switch-sdl2 from renaming it a second time.
# WMMT_NO_NET_STUB excludes net_stub.c (net.c is the real implementation here).
CFLAGS	:=	-g -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES) $(SDL_CFLAGS) \
		-DWMMT_EGL -DWMMT_NO_NET_STUB -DSDL_MAIN_HANDLED -Dmain=sdl_main_entry -DNDEBUG
CFLAGS	+=	$(INCLUDE) -D__SWITCH__
CXXFLAGS:=	$(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

# The GLES2 stack is switch-mesa; -lnx must stay last. sdl2-config supplies
# -lSDL2 (and may already list some of these; duplicates are harmless).
LIBS	:=	$(SDL_LIBS) -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lstdc++ -lz -lnx -lm
LIBDIRS	:=	$(PORTLIBS) $(LIBNX)

# ----------------------------------------------------------------------------
# standard devkitPro two-pass Makefile (outer pass creates the build dir,
# inner pass compiles and links)
# ----------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CC)
export OFILES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o) $(addsuffix .o,$(BINFILES))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(PORTLIBS)/include/SDL2 -I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Add a custom icon.jpg next to this Makefile to replace libnx's default tile.
# Both paths must be absolute: NROFLAGS is exported to the inner make, whose
# CURDIR is platform/switch/build, not platform/switch.
ICON	?=	$(if $(wildcard $(CURDIR)/icon.jpg),$(CURDIR)/icon.jpg,$(LIBNX)/default_icon.jpg)
export SWITCH_ICON	:=	$(ICON)
export NROFLAGS	+=	--icon=$(ICON) --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: preflight $(BUILD) clean all

preflight:
	@for f in ../../src/phys/phys_rc.c ../../src/phys/rc_addr.h ../../src/cardb.h ../../src/halodb.h; do \
		test -f "$$f" || { echo "missing $$f"; echo "run: python build.py data <your dump folder>"; exit 1; }; \
	done
	@test -x "$(SDL2_CONFIG)" || { echo "switch-sdl2 not found at $(SDL2_CONFIG)"; echo "run: pacman -S switch-sdl2 switch-mesa"; exit 1; }
	@test -f "$(PORTLIBS)/include/GLES2/gl2.h" || { echo "switch-mesa GLES2 headers not found"; echo "run: pacman -S switch-mesa"; exit 1; }
	@test -f "$(PORTLIBS)/include/zlib.h" || { echo "switch-zlib not found at $(PORTLIBS)"; echo "run: pacman -S switch-zlib"; exit 1; }

all: preflight $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

.PHONY: all

DEPENDS	:=	$(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(SWITCH_ICON)

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
