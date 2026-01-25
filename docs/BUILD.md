# Building PipeWine

## Prerequisites

- Wine >= 9.0 (with development headers)
- PipeWire >= 1.0 (with development headers)
- GCC with C11 and C++20 support
- Qt6 development packages (for GUI)

### Installing Dependencies

**Arch Linux:**
```bash
sudo pacman -S wine pipewire pipewire-jack qt6-base
```

**Ubuntu/Debian:**
```bash
sudo apt install wine-development libpipewire-0.3-dev qt6-base-dev
```

**Fedora:**
```bash
sudo dnf install wine-devel pipewire-devel qt6-qtbase-devel
```

## Building

### 64-bit Build (Recommended)
```bash
make 64
```

### Debug Build
```bash
DEBUG=true make 64
```

### Building GUI Only
```bash
make -C gui
```

## Installation

```bash
sudo make install64
wine64 regsvr32 pipewine64.dll
```

## Troubleshooting

### Qt tools not found
If you get errors about `uic` or `moc` not found, set the paths manually:
```bash
QT_UIC=/path/to/uic QT_MOC=/path/to/moc make -C gui
```

### PipeWire not found
Ensure pkg-config can find PipeWire:
```bash
pkg-config --cflags --libs libpipewire-0.3
```
