#!/usr/bin/make -f
# Makefile for WineASIO #
# --------------------- #
# Created by falkTX
#

VERSION = 1.2.0

PREFIX ?= /usr

all:
	@echo "error: you must pass '32' or '64' as an argument to this Makefile in order to build WineASIO"

# ---------------------------------------------------------------------------------------------------------------------

32:
	$(MAKE) build ARCH=i386 M=32

64:
	$(MAKE) build ARCH=x86_64 M=64

install32:
	$(MAKE) install ARCH=i386 M=32

install64:
	$(MAKE) install ARCH=x86_64 M=64

register32:
	$(MAKE) register ARCH=i386 M=32

register64:
	$(MAKE) register ARCH=x86_64 M=64

ifeq ($(M),)
install: install32 install64
register: register32 register64
endif

# Test targets

test: 64
	$(MAKE) -C tests check

test-verbose: 64
	$(MAKE) -C tests check VERBOSE=1

.PHONY: test test-verbose

# ---------------------------------------------------------------------------------------------------------------------

clean:
	rm -f *.o *.so
	rm -rf build build32 build64
	rm -rf gui/__pycache__ new_gui/__pycache__
	# Clean GUI generated files
	rm -f gui/*.moc gui/ui_*.hpp
	# Clean test executables
	rm -f tests/*.exe tests/*.o
	# Clean stray build artifacts
	find . -name "*.o" -delete
	find . -name "*.so" -delete

# Complete cleanup including development files
distclean: clean
	rm -f compile_commands.json
	rm -rf .cache
	rm -f *.log

# ---------------------------------------------------------------------------------------------------------------------

tarball: clean
	rm -f ../wineasio-$(VERSION).tar.gz
	tar -c -z \
		--exclude=".git*" \
		--exclude=".travis*" \
		--exclude=debian \
		--exclude=prepare_64bit_asio.sh \
		--exclude=rtaudio/cmake \
		--exclude=rtaudio/contrib \
		--exclude=rtaudio/doc \
		--exclude=rtaudio/tests \
		--exclude=rtaudio/"*.ac" \
		--exclude=rtaudio/"*.am" \
		--exclude=rtaudio/"*.in" \
		--exclude=rtaudio/"*.sh" \
		--exclude=rtaudio/"*.txt" \
		--transform='s,^\.,wineasio-$(VERSION),' \
		-f ../wineasio-$(VERSION).tar.gz .

# ---------------------------------------------------------------------------------------------------------------------

ifneq ($(ARCH),)
ifneq ($(M),)
include Makefile.mk
endif
endif

# ---------------------------------------------------------------------------------------------------------------------
