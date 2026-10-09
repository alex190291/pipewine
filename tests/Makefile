#!/usr/bin/make -f
# Test suite Makefile

CC = winegcc
CFLAGS = -m64 -I.. -I../rtaudio/include $(shell pkg-config --cflags libpipewire-0.3)
LDFLAGS = -m64 -L../build64 -lpipewire-0.3 -lole32 -luuid -lwinmm

# Test executables
TESTS = test_asio_minimal test_buffer test_dsp test_integration test_midi

all: $(TESTS)

test_asio_minimal: test_asio_minimal.c test_common.h asio_test_api.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_buffer: test_buffer.c test_common.h asio_test_api.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_dsp: test_dsp.c test_common.h asio_test_api.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_integration: test_integration.c test_common.h asio_test_api.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_midi: test_midi.c test_common.h asio_test_api.h ../pw_asio_midi.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Run all tests
check: $(TESTS)
	@echo "Running test suite..."
	@for test in $(TESTS); do \
		echo ""; \
		echo "Running $$test..."; \
		./$$test.exe || exit 1; \
	done
	@echo ""
	@echo "All tests passed!"

clean:
	rm -f $(TESTS) test_helper_regressions test_gui_regressions *.exe *.exe.so *.o

.PHONY: all check clean

# Driver behavior regressions use the actual driver/ASIO ABI and fake DSP ports.
# They need Wine, but neither a running PipeWire server nor registration.
REGRESSION_FLAGS = -m64 -D_GNU_SOURCE -std=gnu11 -I.. -I../rtaudio/include $(shell pkg-config --cflags libpipewire-0.3)
REGRESSION_LIBS = ../build64/pw_helper.o ../build64/pw_config_utils.o $(shell pkg-config --libs libpipewire-0.3) -lole32 -luuid -lwinmm -lstdc++

test_driver_regressions.exe: test_driver_regressions.c ../asio.c ../main.c ../build64/pw_helper.o ../build64/pw_config_utils.o
	$(CC) $(REGRESSION_FLAGS) -o $@ $< $(REGRESSION_LIBS)

test_driver_live.exe: test_driver_regressions.c ../asio.c ../main.c ../build64/pw_helper.o ../build64/pw_config_utils.o
	$(CC) $(REGRESSION_FLAGS) -DPWASIO_LIVE_TESTS -o $@ $< $(REGRESSION_LIBS)

test_helper_regressions: test_helper_regressions.cpp ../pw_helper.cpp ../pw_config_utils.c ../pw_helper_common.h
	$(CXX) -std=c++20 -D_GNU_SOURCE -I.. $(shell pkg-config --cflags libpipewire-0.3) -o $@ $< ../pw_config_utils.c $(shell pkg-config --libs libpipewire-0.3) -pthread

regressions: test_helper_regressions test_driver_regressions.exe
	./test_helper_regressions
	./test_driver_regressions.exe

.PHONY: regressions

test_gui_regressions: test_gui_regressions.cpp ../gui/gui.h ../build64/libpwasio_gui.so
	$(CXX) -std=c++20 -fPIC $(shell pkg-config --cflags Qt6Widgets) -o $@ $< -L../build64 -lpwasio_gui $(shell pkg-config --libs Qt6Widgets) -Wl,-rpath,$(abspath ../build64)
