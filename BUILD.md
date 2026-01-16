# Building with Pure ESP-IDF

This project uses pure ESP-IDF (no PlatformIO) with ESP-IDF Component Manager.

## Prerequisites

1. Install ESP-IDF v5.1 or later:
   ```bash
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32s3
   . ./export.sh
   ```

2. Ensure you're in the ESP-IDF environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh  # Adjust path as needed
   ```

## Building

1. Configure the project:
   ```bash
   idf.py set-target esp32s3
   idf.py menuconfig  # Optional: review/change settings
   ```

2. Build the project:
   ```bash
   idf.py build
   ```

3. Flash to device:
   ```bash
   idf.py -p /dev/ttyUSB0 flash  # Adjust port as needed
   ```

4. Monitor serial output:
   ```bash
   idf.py -p /dev/ttyUSB0 monitor
   ```

## Component Dependencies

The project uses ESP-IDF Component Manager to automatically download:
- `espressif/esp32-camera@^2.1.4` - Camera driver for OV3660 sensor

Components are downloaded to `managed_components/` directory automatically during build.

## Camera Configuration

- **Sensor**: OV3660
- **Board**: Elegoo ESP32S3-Camera-v1.3
- **Model**: WROVER_KIT (pin configuration)
- **Target**: ESP32-S3

## Project Structure

```
resp32/
├── CMakeLists.txt          # Main project CMake file
├── idf_component.yml       # Component dependencies (root)
├── sdkconfig.defaults      # Default ESP-IDF configuration
├── partitions.csv          # Partition table
└── src/
    └── main/
        ├── CMakeLists.txt  # Main component build config
        ├── idf_component.yml  # Component dependencies
        ├── main.c          # Application entry point
        └── ...             # Other source files
```

## Troubleshooting

### Component not found
If you get errors about missing components:
```bash
idf.py reconfigure
idf.py build
```

### Clean build
To clean and rebuild:
```bash
idf.py fullclean
idf.py build
```

### Update components
To update managed components:
```bash
rm -rf managed_components
idf.py build
```

