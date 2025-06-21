#!/bin/bash

echo "=== PipeWire ASIO Driver Fix Testing Script ==="
echo

# Set compiler and flags
export CC="gcc"
export CXX="g++"
export WINEDEBUG="-all"  # Disable verbose Wine debug output

# Check if PipeWire is running
echo "Checking PipeWire status..."
if ! pgrep -x "pipewire" > /dev/null; then
    echo "WARNING: PipeWire is not running. Starting PipeWire..."
    pipewire &
    sleep 2
fi

if ! pgrep -x "pipewire-pulse" > /dev/null; then
    echo "WARNING: PipeWire-pulse is not running. Starting PipeWire-pulse..."
    pipewire-pulse &
    sleep 2
fi

echo "PipeWire services are running."
echo

# Build the driver with the fixes (64-bit only as per user preference)
echo "=== Building PipeWire ASIO Driver with Fixes (64-bit) ==="
make clean
make 64 -j$(nproc)

if [ $? -ne 0 ]; then
    echo "ERROR: Driver build failed!"
    exit 1
fi

echo "SUCCESS: Driver built successfully"
echo

# Install the driver (64-bit)
echo "=== Installing Fixed Driver (64-bit) ==="
sudo make install64

if [ $? -ne 0 ]; then
    echo "ERROR: Driver installation failed!"
    exit 1
fi

echo "SUCCESS: Driver installed successfully"
echo

# Build the clean validation test
echo "=== Building Clean Validation Test ==="
winegcc -o simple_validation_test64.exe simple_validation_test.c -lole32 -luuid

if [ $? -ne 0 ]; then
    echo "ERROR: Test build failed!"
    exit 1
fi

echo "SUCCESS: Test built successfully"
echo

# Run the clean validation test
echo "=== Running Clean Validation Test ==="
echo "This test validates the critical fixes:"
echo "  ✓ PipeWire filter state transitions"
echo "  ✓ Buffer allocation race condition fixes"
echo "  ✓ Process callback safety"
echo

wine simple_validation_test64.exe

test_result=$?
echo

if [ $test_result -eq 0 ]; then
    echo "🎉 SUCCESS: All fixes validated successfully!"
    echo "The PipeWire ASIO driver is working correctly."
else
    echo "❌ FAILURE: Some issues remain"
    echo "Check the test output above for details."
fi

echo
echo "=== Test Complete ==="
exit $test_result 