#!/usr/bin/make -f
# Makefile for PipeWireASIO #
# ------------------------- #
# Streamlined for PipeWire only

ifeq ($(ARCH),)
$(error incorrect use of Makefile, ARCH var is missing)
endif
ifeq ($(M),)
$(error incorrect use of Makefile, M var is missing)
endif

# ---------------------------------------------------------------------------------------------------------------------
# Dependency version checks
# ---------------------------------------------------------------------------------------------------------------------

# Check for required PipeWire development files
PIPEWIRE_VERSION := $(shell pkg-config --modversion libpipewire-0.3 2>/dev/null)
ifeq ($(PIPEWIRE_VERSION),)
$(error PipeWire development files not found. Install libpipewire-0.3-dev or pipewire-devel)
endif

# Check for Wine (optional but needed for testing)
WINE_VERSION := $(shell wine --version 2>/dev/null | head -1)
ifeq ($(WINE_VERSION),)
$(warning Wine not found in PATH - build may succeed but testing will require Wine)
endif

# Print build configuration
$(info Building PipeWireASIO for $(ARCH))
$(info   PipeWire version: $(PIPEWIRE_VERSION))
ifneq ($(WINE_VERSION),)
$(info   Wine version: $(WINE_VERSION))
endif

# ---------------------------------------------------------------------------------------------------------------------

export wineasio_dll_MODULE = pipewine$(M).dll
export wineasio_dll_SPEC = pipewine$(M).dll.spec

# Simplified GUI build
libpwasio_gui: build$(M)/pw_helper.o build$(M)/pw_config_utils.o
	$(MAKE) -C gui ../build$(M)/libpwasio_gui.so

build$(M)/pw_helper.o: pw_helper.cpp pw_thread.inc.cpp
	@$(shell mkdir -p build$(M))
	$(WINECXX) -c $(INCLUDE_PATH) $(CFLAGS) $(filter-out -std=gnu11 -Wdeclaration-after-statement -Werror=implicit-function-declaration,$(CEXTRA)) -std=c++20 -fno-gnu-unique -MMD -MP -o $@ $<

build$(M)/pw_config_utils.o: pw_config_utils.c
	@$(shell mkdir -p build$(M))
	$(CC) -c $(INCLUDE_PATH) $(CFLAGS) $(CEXTRA) -MMD -MP -o $@ $<

PREFIX                = /usr
SRCDIR                = .
DLLS                  = $(wineasio_dll_MODULE) $(wineasio_dll_MODULE).so

### Tools
CC        = gcc
WINE      = wine
WINEBUILD = winebuild
WINECXX   = wineg++

ifeq ($(M),64)
WINE      = wine64
endif

### Streamlined settings focused on PipeWire
CEXTRA                = -m$(M) -D_REENTRANT -fPIC -Wall -pipe -std=gnu11 -D_GNU_SOURCE
CEXTRA               += -fno-strict-aliasing -Wwrite-strings -Wpointer-arith
CEXTRA               += -Werror=implicit-function-declaration
CEXTRA               += $(shell pkg-config --cflags libpipewire-0.3)
CEXTRA               += '-DDRIVER_DLL="$(wineasio_dll_MODULE)"'
CEXTRA               += -DPIPEWIRE_ASIO_DRIVER  # Define to distinguish from JACK version

# Simplified include paths - focus on essential directories
INCLUDE_PATH          = -I. -Irtaudio/include
INCLUDE_PATH         += $(shell pkg-config --cflags-only-I libpipewire-0.3)
INCLUDE_PATH         += -I$(PREFIX)/include/wine
INCLUDE_PATH         += -I$(PREFIX)/include/wine/windows

# PipeWire libraries only
LIBRARIES             = $(shell pkg-config --libs libpipewire-0.3)

# 64bit build needs an extra flag
ifeq ($(M),64)
CEXTRA               += -DNATIVE_INT64
endif

# Debug or Release
ifeq ($(DEBUG),true)
CEXTRA               += -O0 -DDEBUG -g -D__WINESRC__
else
CEXTRA               += -O2 -DNDEBUG -fvisibility=hidden
endif

### pipewine.dll settings
wineasio_dll_C_SRCS   = asio.c \
			main.c \
			regsvr.c

# Simplified linker flags
wineasio_dll_LDFLAGS  = -shared \
			-m$(M) \
			-mnocygwin \
			$(wineasio_dll_SPEC) \
			-L/usr/lib$(M)/wine \
			-L/usr/lib/$(ARCH)-linux-gnu/wine

wineasio_dll_DLLS     = odbc32 \
			ole32 \
			winmm
wineasio_dll_LIBRARIES = uuid

wineasio_dll_OBJS     = $(wineasio_dll_C_SRCS:%.c=build$(M)/%.c.o) build$(M)/pw_helper.o build$(M)/pw_config_utils.o

### Global source lists

C_SRCS                = $(wineasio_dll_C_SRCS)

### Generic targets

all:
build: rtaudio/include/asio.h $(DLLS:%=build$(M)/%) libpwasio_gui

install:
	install build$(M)/$(wineasio_dll_MODULE) $(PREFIX)/lib$(M)/wine/$(ARCH)-windows/
	install build$(M)/$(wineasio_dll_MODULE).so $(PREFIX)/lib$(M)/wine/$(ARCH)-unix/
	install build$(M)/libpwasio_gui.so $(PREFIX)/lib$(M)/wine/$(ARCH)-unix/

register:
	$(WINE) regsvr32 $(wineasio_dll_MODULE)

### Build rules

.PHONY: all build install register libpwasio_gui

# Implicit rules

build$(M)/%.c.o: %.c
	@$(shell mkdir -p build$(M))
	$(CC) -c $(INCLUDE_PATH) $(CFLAGS) $(CEXTRA) -MMD -MP -o $@ $<

### Target specific build rules

build$(M)/$(wineasio_dll_MODULE): $(wineasio_dll_OBJS)
	$(WINEBUILD) -m$(M) --dll --fake-module -N $(wineasio_dll_MODULE) -E $(wineasio_dll_SPEC) $^ -o $@

build$(M)/$(wineasio_dll_MODULE).so: $(wineasio_dll_OBJS)
	$(WINECXX) $^ $(wineasio_dll_LDFLAGS) $(LIBRARIES) \
		$(wineasio_dll_DLLS:%=-l%) $(wineasio_dll_LIBRARIES:%=-l%) -o $@ \
		-Wl,-soname,$(wineasio_dll_MODULE).so

# ---------------------------------------------------------------------------------------------------------------------
# Development tools
# ---------------------------------------------------------------------------------------------------------------------

# Generate compile_commands.json for IDE support (clangd, etc.)
compile_commands.json:
	@echo "Generating compile_commands.json..."
	@if command -v bear >/dev/null 2>&1; then \
		bear -- $(MAKE) -B 64 2>/dev/null; \
	elif command -v compiledb >/dev/null 2>&1; then \
		compiledb make -B 64 2>/dev/null; \
	else \
		echo "Install 'bear' or 'compiledb' for compile_commands.json generation"; \
		exit 1; \
	fi

.PHONY: compile_commands.json

-include $(wineasio_dll_OBJS:.o=.d)
