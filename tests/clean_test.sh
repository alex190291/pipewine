#!/bin/bash

echo "=== Clean PipeWire ASIO Driver Validation ==="
echo

# Set minimal Wine debug output
export WINEDEBUG=-all

# Check PipeWire status
echo "Checking PipeWire status..."
if ! pgrep -x "pipewire" > /dev/null; then
    echo "Starting PipeWire..."
    pipewire &
    sleep 1
fi

echo "✓ PipeWire is running"
echo

# Build the driver
echo "Building PipeWire ASIO Driver (64-bit)..."
make clean > /dev/null 2>&1
make 64 -j$(nproc) > /dev/null

if [ $? -ne 0 ]; then
    echo "❌ Driver build failed!"
    echo "Building with verbose output to see errors:"
    make 64
    exit 1
fi

echo "✓ Driver built successfully"

# Install the driver
echo "Installing driver..."
sudo make install64 > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "❌ Driver installation failed!"
    exit 1
fi

echo "✓ Driver installed"

# Build the test program
echo "Building validation test..."
winegcc -o simple_validation_test64.exe simple_validation_test.c -lole32 -luuid > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "❌ Test build failed!"
    echo "Building with verbose output:"
    winegcc -o simple_validation_test64.exe simple_validation_test.c -lole32 -luuid
    exit 1
fi

echo "✓ Test program built"
echo

# Run the validation test
echo "Running focused validation test..."
echo "==============================================="
wine64 simple_validation_test64.exe

test_result=$?
echo "==============================================="

# Clean up
echo
echo "Cleaning up test files..."
rm -f simple_validation_test64.exe > /dev/null 2>&1

# Report results
if [ $test_result -eq 0 ]; then
    echo "🎉 VALIDATION PASSED: PipeWire ASIO driver fixes are working correctly!"
else
    echo "❌ VALIDATION FAILED: Issues remain in the driver"
fi

exit $test_result 