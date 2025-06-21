# PipeWire Quantum Synchronization Guide

## The Problem

When using ASIO drivers with PipeWire, timing errors and callback issues can occur when the PipeWire quantum (buffer size) doesn't match the ASIO buffer size. This causes:

- Callback timing errors (red bars in VBAsioTest)
- Audio dropouts and glitches
- Inconsistent latency
- Poor performance with small buffer sizes (128 samples)

## The Solution: GUI-Based Quantum Setting

The PipeWire ASIO driver now includes an intelligent quantum synchronization system that automatically configures PipeWire for optimal performance.

### How It Works

1. **GUI Integration**: When you save configuration in the ASIO control panel, the GUI automatically sets the PipeWire quantum to match your buffer size
2. **Pre-Configuration**: The quantum is set before the driver initializes, ensuring perfect synchronization from the start
3. **No Driver Overhead**: The driver no longer needs to make system calls during audio processing

### Usage Methods

#### Method 1: Automatic (Recommended)
1. Open your DAW (e.g., Ableton Live)
2. Go to ASIO driver settings and open the PipeWireASIO control panel
3. Set your desired buffer size (e.g., 256 samples)
4. Click "OK" to save - **the quantum is automatically set!**
5. Restart your DAW for optimal performance

#### Method 2: Manual Script
```bash
# Set quantum to 256 samples
./set-pipewire-quantum.sh 256

# Set quantum to 128 samples  
./set-pipewire-quantum.sh 128

# Set quantum to 512 samples
./set-pipewire-quantum.sh 512
```

#### Method 3: Direct Command
```bash
# Set quantum manually
pw-metadata -n settings 0 clock.quantum 256

# Check current quantum
pw-metadata -n settings | grep quantum
```

## Benefits

- **Perfect Sync**: PipeWire quantum exactly matches ASIO buffer size
- **Zero Timing Errors**: Eliminates callback timing mismatches
- **Better Performance**: Especially noticeable with small buffer sizes (128-256 samples)
- **Professional Reliability**: Consistent performance for audio production

## Troubleshooting

### If pw-metadata is not found:
```bash
# Fedora
sudo dnf install pipewire-utils

# Ubuntu/Debian  
sudo apt install pipewire-utils
```

### If timing errors persist:
1. Ensure quantum is set correctly: `pw-metadata -n settings | grep quantum`
2. Restart your DAW after changing quantum
3. Try the manual script: `./set-pipewire-quantum.sh [your_buffer_size]`

## Technical Details

The quantum setting tells PipeWire how many audio samples to process in each cycle. When this matches your ASIO buffer size:

- **Audio callbacks are perfectly aligned**
- **No buffer size conversion overhead**  
- **Minimal latency and jitter**
- **Optimal CPU usage**

This revolutionary approach eliminates the timing issues that plagued earlier versions and provides professional-grade audio performance. 