#!/bin/bash

echo "=== PipeWire ASIO Driver Sync Mode Testing ==="
echo "Testing different Wine synchronization modes for optimal timing..."
echo

# Function to run test with timeout and capture results
run_test() {
    local mode="$1"
    local env_vars="$2"
    local log_file="test_${mode}.log"
    
    echo "Testing $mode mode..."
    wineserver -k
    sleep 2
    
    # Run test with timeout
    timeout 20s bash -c "$env_vars wine VBASIOTest64.exe" > "$log_file" 2>&1
    
    # Extract timing results
    echo "=== $mode Results ==="
    if grep -q "CALLBACK TIMING ERROR" "$log_file"; then
        echo "❌ Timing errors found:"
        grep "CALLBACK TIMING ERROR" "$log_file" | head -3
    else
        echo "✅ No timing errors detected"
    fi
    
    if grep -q "DSP LOAD" "$log_file"; then
        echo "DSP Load info:"
        grep "DSP LOAD" "$log_file" | head -3
    fi
    
    if grep -q "Running Time" "$log_file"; then
        echo "Runtime info:"
        grep "Running Time" "$log_file" | head -1
    fi
    
    echo "---"
    echo
}

# Test 1: No sync (baseline)
run_test "NOSYNC" ""

# Test 2: ESYNC
run_test "ESYNC" "WINEESYNC=1"

# Test 3: FSYNC (if available)
run_test "FSYNC" "WINEFSYNC=1"

# Test 4: ESYNC + Real-time optimizations
run_test "ESYNC_RT" "WINEESYNC=1 WINEDEBUG=-all WINE_RT_POLICY=1"

# Test 5: FSYNC + Real-time optimizations  
run_test "FSYNC_RT" "WINEFSYNC=1 WINEDEBUG=-all WINE_RT_POLICY=1"

echo "=== Summary ==="
echo "Check individual log files for detailed results:"
ls -la test_*.log 2>/dev/null || echo "No log files created"

echo
echo "=== Recommendations ==="
echo "1. Use WINEESYNC=1 for better synchronization"
echo "2. Use WINEFSYNC=1 if your kernel supports futex2 (5.16+)"
echo "3. Set WINEDEBUG=-all to reduce overhead"
echo "4. Consider real-time kernel for professional audio"
echo "5. Adjust PipeWire quantum settings: pw-metadata -n settings 0 clock.force-quantum 1024" 