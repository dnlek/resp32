# Quick Start Guide - Pure ESP-IDF Build

## One-Time Setup

1. **Install ESP-IDF** (if not already installed):
   ```bash
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32s3
   . ./export.sh
   ```

2. **Add to your shell profile** (e.g., `~/.zshrc` or `~/.bashrc`):
   ```bash
   alias get_idf='. $HOME/esp/esp-idf/export.sh'
   ```

## Daily Usage

1. **Open terminal and activate ESP-IDF**:
   ```bash
   get_idf  # or: . $HOME/esp/esp-idf/export.sh
   ```

2. **Navigate to project**:
   ```bash
   cd /path/to/resp32
   ```

3. **Set target** (first time only):
   ```bash
   idf.py set-target esp32s3
   ```

4. **Build**:
   ```bash
   idf.py build
   ```

5. **Flash and monitor**:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   (Replace `/dev/ttyUSB0` with your port: `/dev/tty.usbserial-*` on macOS, `COM3` on Windows)

## Common Commands

```bash
# Build only
idf.py build

# Flash only
idf.py -p /dev/ttyUSB0 flash

# Monitor only (Ctrl+] to exit)
idf.py -p /dev/ttyUSB0 monitor

# Flash + Monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Clean build
idf.py fullclean
idf.py build

# Menuconfig (configure project)
idf.py menuconfig

# Reconfigure (after component changes)
idf.py reconfigure
```

## Component Management

Components are automatically downloaded to `managed_components/` during build.

To update components:
```bash
rm -rf managed_components
idf.py build
```

## Project Info

- **Target**: ESP32-S3
- **Camera**: OV3660 (3MP) on Elegoo ESP32S3-Camera-v1.3
- **Camera Driver**: esp32-camera 2.1.4 (managed component)
- **Framework**: Pure ESP-IDF (no PlatformIO)

## Troubleshooting

**Port not found?**
- Linux: `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`
- macOS: `ls /dev/tty.usbserial-*` or `ls /dev/cu.*`
- Windows: Check Device Manager for COM port

**Permission denied?**
```bash
sudo chmod 666 /dev/ttyUSB0  # Linux
# Or add user to dialout group: sudo usermod -a -G dialout $USER
```

**Build errors?**
```bash
idf.py fullclean
idf.py reconfigure
idf.py build
```

