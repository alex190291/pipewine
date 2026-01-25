#!/usr/bin/make -f
# Test suite Makefile

CC = winegcc
CFLAGS = -m64 -I.. -I../rtaudio/include $(shell pkg-config --cflags libpipewire-0.3)
LDFLAGS = -m64 -L../build64 -lpipewire-0.3 -lole32 -luuid -lwinmm

# Test executables
TESTS = test_asio_minimal test_buffer test_dsp test_integration test_midi

all: $(TESTS)

test_asio_minimal: test_asio_minimal.c test_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_buffer: test_buffer.c test_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_dsp: test_dsp.c test_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_integration: test_integration.c test_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_midi: test_midi.c test_common.h ../pw_asio_midi.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Run all tests
check: $(TESTS)
	@echo "Running test suite..."
	@for test in $(TESTS); do \
		echo ""; \
		echo "Running $$test..."; \
		wine ./$$test.exe || exit 1; \
	done
	@echo ""
	@echo "All tests passed!"

clean:
	rm -f $(TESTS) *.exe *.o

.PHONY: all check clean
