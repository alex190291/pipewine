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
./install.sh
```

Run as your normal user. This builds and installs the 64-bit driver and control
panel into `WINEPREFIX` (default `~/.wine`), registers the driver, and saves backups
under `<prefix>/.pipewine-backups`. No system Wine or Proton files are modified.

To reuse a build or select another Wine prefix/runtime:

```bash
./install.sh --no-build --prefix /path/to/prefix --wine /path/to/wine64
```

Launch applications using `<prefix>/pipewine-run /path/to/application.exe`.
This wrapper supplies the prefix's driver search path, which is required for
loading this ELF-based Wine driver with current system Wine. Existing Ableton
`launch-ableton.sh` scripts in the prefix are updated automatically; restart
Ableton from its shortcut after installation. Use `--no-ableton` to skip those edits.

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
