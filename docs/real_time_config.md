# Real-Time Configuration Updates

## Overview

PipeWireASIO now supports real-time configuration updates, allowing you to change settings without restarting the ASIO driver or your audio applications. This feature was implemented as part of Task 6.2.1.

## How It Works

The real-time configuration update system works by:

1. **Configuration File Monitoring**: The driver can reload configuration from the `pipewireasio.conf` file at runtime
2. **GUI Integration**: When you make changes in the GUI and click Apply/OK, the configuration is immediately reloaded and applied
3. **Intelligent Updates**: The system detects what has changed and applies only the necessary updates
4. **ASIO Host Notification**: When appropriate, the driver notifies the ASIO host application of changes

## Supported Configuration Changes

### Immediate Effect (No Restart Required)
- **Device Selection**: Input/output device changes take effect immediately
- **Auto-Connect Setting**: Changes to hardware auto-connection behavior
- **Logging Configuration**: Debug logging level and enable/disable changes
- **Client Name**: PipeWire client name changes (for new connections)

### Deferred Effect (Requires Buffer Recreation)
- **Channel Count**: Input/output channel count changes require the ASIO host to recreate buffers
- **Buffer Size**: Buffer size changes take effect when the ASIO host next calls CreateBuffers
- **Sample Rate**: Sample rate changes may require application restart for full effect

## Usage

### Via GUI
1. Open the ASIO control panel from your audio application
2. Make desired changes in the GUI
3. Click "OK" or "Apply"
4. Changes are automatically applied in real-time

### Via Configuration File
1. Edit your configuration file:
   - User config: `~/.config/pipewireasio/pipewireasio.conf`
   - System config: `/etc/pipewireasio/pipewireasio.conf`
2. Save the file
3. Trigger a reload using one of these methods:
   - Open and close the GUI (triggers reload)
   - Use the custom ASIO Future selector (for advanced users)

### Via Environment Variables
Environment variables still take precedence and are re-evaluated during configuration reload:
- `PWASIO_NUMBER_INPUTS`
- `PWASIO_NUMBER_OUTPUTS`
- `PWASIO_CONNECT_TO_HARDWARE`
- `PWASIO_BUFFERSIZE`

## Technical Details

### Configuration Precedence
1. **Environment Variables** (highest priority)
2. **User Configuration File** (`~/.config/pipewireasio/pipewireasio.conf`)
3. **System Configuration File** (`/etc/pipewireasio/pipewireasio.conf`)
4. **Registry Settings** (lowest priority, for compatibility)

### ASIO Host Notifications
When configuration changes occur, the driver may send notifications to the ASIO host:
- `kAsioResyncRequest`: Sent when channel counts change
- `kAsioLatenciesChanged`: Sent when sample rate or device changes occur

### Custom ASIO Future Selector
For advanced users and applications, a custom ASIO Future selector `0x12345678` is available to trigger configuration reload programmatically:

```c
// Example usage in ASIO application
ASIOError result = ASIOFuture(0x12345678, NULL);
if (result == ASE_OK) {
    // Configuration reloaded successfully
}
```

## Limitations

### What Cannot Be Changed at Runtime
- **Driver Architecture**: Core PipeWire filter setup cannot be changed without restart
- **Buffer Format**: Audio sample format changes require restart
- **Thread Configuration**: Real-time thread settings require restart

### ASIO Host Limitations
Some changes require cooperation from the ASIO host application:
- **Channel count changes**: Host must recreate buffers to see new channel configuration
- **Buffer size changes**: Host must call CreateBuffers again with new buffer size
- **Sample rate changes**: Some hosts may need to be restarted for proper sample rate handling

## Troubleshooting

### Configuration Not Applied
1. Check that the configuration file syntax is correct
2. Verify file permissions (must be readable by the Wine process)
3. Check the debug log for error messages (enable with `debug_logging = true`)

### Partial Configuration Applied
Some settings may require additional steps:
- Channel count changes: Restart your audio application or recreate ASIO buffers
- Sample rate changes: Check if your application supports runtime sample rate changes

### Debug Logging
Enable debug logging to see configuration reload activity:
```ini
[advanced]
debug_logging = true
log_level = 3
```

Log messages will show:
- Configuration file loading
- Settings that changed
- Actions taken (device reconnection, etc.)
- Any errors encountered

## Examples

### Example 1: Changing Audio Device
1. Edit `~/.config/pipewireasio/pipewireasio.conf`:
```ini
[devices]
output_device = "My New Audio Interface"
```
2. Open the ASIO control panel and click OK
3. The driver immediately connects to the new device

### Example 2: Adjusting Channel Count
1. Edit configuration:
```ini
[audio]
output_channels = 8
```
2. Reload configuration via GUI
3. Restart your audio application to see the new channels

### Example 3: Debug Mode
1. Enable debugging:
```ini
[advanced]
debug_logging = true
log_level = 3
```
2. Reload configuration
3. Check Wine debug output for detailed information

## Implementation Notes

This feature is implemented in:
- `asio.c`: `reload_configuration_runtime()` and `apply_configuration_changes()` functions
- `GuiApplyConfig()`: Modified to trigger real-time reload
- Custom ASIO Future selector for programmatic access

The implementation ensures thread safety and proper error handling while maintaining compatibility with existing ASIO applications. 