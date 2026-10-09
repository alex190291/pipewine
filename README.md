<div align="center">
  <img src="pipewine.png" alt="PipeWine Logo" width="128" height="128">
  
  # PipeWine 🎵
  
  **Professional ASIO Driver for PipeWire on Linux**
</div>

PipeWine is a high-performance ASIO (Audio Stream Input/Output) driver that enables professional audio applications running under Wine to use PipeWire for low-latency audio processing. It provides native integration between Windows audio applications and the Linux PipeWire audio system.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/yourusername/pipewine)
[![License](https://img.shields.io/badge/license-LGPL--2.1%2B-blue.svg)](LICENSE)
[![Wine](https://img.shields.io/badge/wine-9.0%2B-red.svg)](https://www.winehq.org/)
[![PipeWire](https://img.shields.io/badge/pipewire-1.0%2B-orange.svg)](https://pipewire.org/)

## ✨ Features

### 🎯 **Audio Performance**
- **Low latency** audio processing capabilities
- **Real-time audio processing** on a Wine-created PipeWire data thread
- **Validated buffer management** with allocation failure cleanup
- **Quantum/rate requests** scoped to the active ASIO node

### 🎛️ **Flexible Configuration**
- **GUI control panel** with safe modal lifetime and saved preferences
- **File-based configuration** system (`pipewine.conf`)
- **Configuration changes** saved safely; buffer/rate changes require host reset, channel/device changes require reopening
- **Multiple configuration locations** (user and system-wide)

### 🔧 **Advanced Features**
- **Sample rate selection** (44.1kHz, 48kHz, 96kHz, 192kHz); dispose buffers before changing rate
- **Configurable buffer sizes** (16-8192 samples, powers of 2)
- **Multiple channel configurations** (up to 64 inputs/outputs)
- **Automatic audio connections** to selected/default devices, with hot-plug reconnection
- **Memory leak prevention** with comprehensive buffer management

### 🖥️ **Application Compatibility**
- **VBASIOTest64** ✅ (Tested and working)
- **Ableton Live 11** ✅ (Tested and working)
- **Other applications** ❓ (Not yet tested - compatibility unknown)

## 🚀 Quick Start

### Prerequisites
- Linux system with PipeWire
- Wine 9.0 or later
- 64-bit architecture

### Installation
```bash
# Clone the repository
git clone https://github.com/alex190291/pipewine.git
cd pipewine

# Build and install into ~/.wine (run without sudo)
./install.sh
```

The installer copies the 64-bit driver and control panel into the selected Wine
prefix, registers PipeWine, and backs up replaced files under `.pipewine-backups`.
It creates `<prefix>/pipewine-run`, which supplies Wine's driver search path, and
updates existing `launch-ableton.sh` scripts under `drive_c/ProgramData/Ableton` to
use it. Restart Ableton from its shortcut after installing.

Use `./install.sh --prefix /path/to/prefix` for another prefix, `--no-build` to use
an existing build, or `--wine /path/to/wine64` for a custom Wine runtime.
The default prefix follows `WINEPREFIX`, falling back to `~/.wine`. System Wine
and Proton installations are untouched. Other application launchers should use
`<prefix>/pipewine-run` instead of `wine`; use `--no-ableton` to skip launcher edits.

### Basic Usage
```bash
# Test the installation
~/.wine/pipewine-run VBASIOTest64.exe

# Launch your DAW and select "PipeWine" as ASIO driver
~/.wine/pipewine-run ~/.wine/drive_c/path/to/your/daw.exe
```
## ⚙️ Configuration

### Quick Configuration
The driver can be configured through multiple methods:

1. **GUI Control Panel**: Access through your audio application's ASIO settings
2. **Configuration File**: Edit `~/.config/pipewine/pipewine.conf`
3. **Environment Variables**: Set `PWASIO_*` variables
4. **Registry Settings**: Use Wine registry editor

### Example Configuration
```ini
[audio]
sample_rate = 48000
buffer_size = 512
input_channels = 2
output_channels = 2

[devices]
auto_connect = true

[performance]
rt_priority = 10
```

The driver requests matching quantum and rate while its ASIO node is active.
The host can request another supported buffer size unless fixed buffer mode is enabled.
See [quantum behavior](docs/QUANTUM_SYNC_GUIDE.md) for graph conflicts and reset requirements.

## ✨ Known Issues:
You need to disable your DAW's audio engine, otherwise programs started after the DAW can't create PipeWire portals.

## 🎵 Audio Applications

### Tested Applications
| Application | Status | Notes |
|-------------|--------|-------|
| VBASIOTest64 | ✅ Working | Basic ASIO functionality confirmed |
| Ableton Live 11 | ✅ Working | Successfully tested with audio playback |

### Performance Notes
- Performance benchmarks are not yet available
- Latency depends on buffer size configuration and PipeWire quantum settings
- The driver requests matching quantum/rate; competing graph constraints can temporarily cause silence

## 🛠️ Building from Source

### Build Dependencies
```bash
# Fedora/RHEL
sudo dnf install gcc gcc-c++ make wine-devel pipewire-devel qt6-qtbase-devel

# Ubuntu/Debian  
sudo apt install build-essential wine-dev libpipewire-0.3-dev qt6-base-dev

# Arch Linux
sudo pacman -S base-devel wine pipewire qt6-base
```

### Build Commands
```bash
# 64-bit build (recommended)
make 64

# Debug build
DEBUG=true make 64

# Clean build
make clean
```

## 🐛 Troubleshooting

### Common Issues

**Q: Driver not appearing in ASIO applications**
```bash
# Re-register the driver
./install.sh --no-build
```

For a driver installed into a prefix, launch applications through its
`pipewine-run` wrapper so Wine can find the driver and its control panel.

**Q: High latency or audio dropouts**

Try a larger ASIO host buffer and inspect the graph with `pw-top`. The driver
requests the host quantum while active. See [the quantum guide](docs/QUANTUM_SYNC_GUIDE.md).

For implementation fixes, validation, and further optimization opportunities,
see [the driver review](docs/DRIVER_REVIEW.md).

## 🤝 Contributing

We welcome contributions!

### Development Setup
```bash
git clone https://github.com/alex190291/pipewine.git
cd pipewine
make 64
./tests/run_regressions.sh
```

### Code Style
- Follow Linux kernel coding style
- Use meaningful variable names
- Add comments for complex algorithms
- Test changes thoroughly

## 📄 License

### Copyright Notice

```
Copyright (C) 2006 Robert Reif
Portions copyright (C) 2007 Ralf Beck
Portions copyright (C) 2007 Johnny Petrantoni
Portions copyright (C) 2007 Stephane Letz
Portions copyright (C) 2008 William Steidtmann
Portions copyright (C) 2010 Peter L Jones
Portions copyright (C) 2010 Torben Hohn
Portions copyright (C) 2010 Nedko Arnaudov
Portions copyright (C) 2011 Christian Schoenebeck
Portions copyright (C) 2013 Joakim Hernberg
Portions copyright (C) 2020-2023 Filipe Coelho
```

### License Terms

- **Library Code**: The WineASIO library code is licensed under **LGPL v2.1** or later. See `COPYING.LIB` for details.
- **GUI Code**: The WineASIO settings UI code is licensed under **GPL v2+** or later. See `COPYING.GUI` for details.

### LGPL v2.1 Permissions

This allows:
- ✅ Commercial use
- ✅ Distribution
- ✅ Modification
- ✅ Patent use
- ✅ Private use

With requirements:
- 📋 License and copyright notice
- 📋 State changes
- 📋 Disclose source (for distributions)
- 📋 Same license (for modifications)

## 🙏 Acknowledgments

- **WineASIO Project** - Original ASIO implementation for Wine
- **RtAudio Project** - Cross-platform audio I/O library and inspiration
- **Wine Project** - Windows compatibility layer
- **PipeWire Team** - Modern Linux audio infrastructure
- **ASIO SDK** - Steinberg's Audio Stream I/O SDK
- **Linux Audio Community** - Continuous support and feedback

## 📞 Support

- **🐛 Bug Reports**: [GitHub Issues](https://github.com/yourusername/pipewine/issues)
- **💬 Discussions**: [GitHub Discussions](https://github.com/yourusername/pipewine/discussions)
- **📚 Documentation**: [Project Wiki](https://github.com/yourusername/pipewine/wiki)
- **📧 Email**: attax@nosinu-records.com

## 🌟 Star History

If PipeWine helps your audio workflow, please consider giving it a star! ⭐

[![Star History Chart](https://api.star-history.com/svg?repos=alex190291/pipewine&type=Date)](https://star-history.com/#alex190291/pipewine&Date)

---

**Made with ❤️ for the Linux audio community**
