# Migration to Pure ESP-IDF with esp32-camera 2.1.4

## Summary

This project has been migrated from PlatformIO to pure ESP-IDF with ESP-IDF Component Manager. The camera driver now uses `espressif/esp32-camera@^2.1.4` from the component registry.

## Changes Made

### 1. Component Management
- ✅ Removed local `components/esp32-camera/` directory
- ✅ Added `idf_component.yml` files for component dependencies
- ✅ Configured to use managed component `espressif/esp32-camera@^2.1.4`

### 2. Camera Configuration
- ✅ Camera model: `CAMERA_MODEL_WROVER_KIT` (for Elegoo ESP32S3-Camera-v1.3 board)
- ✅ Sensor: OV3660 (3MP)
- ✅ Frame size: SVGA (800x600) with PSRAM, QVGA (320x240) without
- ✅ Double buffering enabled when PSRAM available
- ✅ Optimized for ESP32-S3

### 3. Build System
- ✅ Updated `CMakeLists.txt` files
- ✅ Added ESP32-S3 target configuration
- ✅ Removed PlatformIO-specific configurations

### 4. Documentation
- ✅ Created `BUILD.md` with ESP-IDF build instructions
- ✅ Updated `sdkconfig.defaults` for ESP32-S3

## Project Structure

```
resp32/
├── CMakeLists.txt              # Main project CMake
├── idf_component.yml          # Root component dependencies
├── sdkconfig.defaults         # ESP-IDF default config
├── partitions.csv             # Partition table
├── BUILD.md                   # Build instructions
├── src/
│   ├── main/
│   │   ├── CMakeLists.txt     # Main component config
│   │   ├── idf_component.yml  # Component dependencies
│   │   ├── main.c             # Application entry
│   │   └── ...
│   ├── app_httpd.cpp          # HTTP camera server
│   ├── wifi_provisioning.c    # WiFi provisioning
│   ├── socket_server.c        # Robot control socket
│   └── robot_control.c        # Robot control logic
└── managed_components/        # Auto-downloaded components
    └── espressif__esp32-camera/  # Camera driver 2.1.4
```

## Building

### Prerequisites
1. Install ESP-IDF v5.1+:
   ```bash
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32s3
   . ./export.sh
   ```

2. Set up environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh  # Adjust path as needed
   ```

### Build Steps
```bash
cd resp32
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Camera Hardware

- **Board**: Elegoo ESP32S3-Camera-v1.3
- **Sensor**: OV3660 (3MP)
- **Pin Configuration**: WROVER_KIT model
- **PSRAM**: Required for best performance (8MB recommended)

## Key Features

1. **WiFi Provisioning**: Captive portal for easy setup
2. **Camera Streaming**: HTTP server on port 80
3. **Robot Control**: Socket server on port 100
4. **Auto-reconnect**: Handles WiFi disconnections
5. **Reset Button**: GPIO 0 held for 5 seconds resets WiFi

## Troubleshooting

### Component Not Found
```bash
idf.py reconfigure
idf.py build
```

### Clean Build
```bash
idf.py fullclean
idf.py build
```

### Update Components
```bash
rm -rf managed_components
idf.py build
```

## Differences from PlatformIO

1. **No `platformio.ini`**: Uses ESP-IDF CMake system
2. **Component Manager**: Uses `idf_component.yml` instead of `lib_deps`
3. **Build Command**: `idf.py build` instead of `pio run`
4. **Flash Command**: `idf.py flash` instead of `pio run -t upload`
5. **Component Location**: `managed_components/` instead of `.pio/libdeps/`

## Next Steps

1. Test camera initialization
2. Verify WiFi provisioning works
3. Test camera streaming endpoints
4. Test robot control socket server
5. Optimize camera settings for your use case

