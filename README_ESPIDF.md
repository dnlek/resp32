# ESP-IDF Robot Camera Server

This project has been converted to use ESP-IDF framework with WiFi provisioning and captive portal support.

## Project Structure

```
src/
  main/
    CMakeLists.txt    # Component build configuration
    main.c            # Application entry point
  app_httpd.cpp       # HTTP server (ESP-IDF compatible)
  wifi_provisioning.c # WiFi provisioning with captive portal
  socket_server.c     # Socket server for robot control
  robot_control.c     # Robot control and factory test
  camera_pins.h       # Camera pin definitions
  app_httpd.h         # HTTP server header
```

## Features

- **WiFi Provisioning**: Captive portal for easy WiFi setup
- **Auto-reconnect**: Automatically reconnects if WiFi disconnects
- **Reset Button**: Hold GPIO 0 for 5 seconds to reset WiFi credentials
- **Socket Server**: Port 100 for robot control
- **Camera Server**: Port 80 (when enabled)
- **Factory Test**: Serial2 communication for factory testing

## Building

```bash
pio run
```

## Uploading

```bash
pio run -t upload
```

## WiFi Provisioning

1. On first boot (or after reset), the device creates a WiFi AP named `RobotSetup-XXXX` (where XXXX is MAC address)
2. Connect to this network (no password)
3. Captive portal should open automatically, or navigate to `http://192.168.4.1`
4. Select your WiFi network and enter password
5. Device will connect and save credentials to NVS

## Reset WiFi

Hold the BOOT button (GPIO 0) for 5 seconds to clear WiFi credentials and restart in provisioning mode.

## Differences from Arduino Version

- Uses ESP-IDF `wifi_provisioning` component instead of WiFiManager
- Uses FreeRTOS tasks instead of `setup()`/`loop()`
- Uses ESP-IDF logging (`ESP_LOGI`, `ESP_LOGE`) instead of `Serial.println()`
- Uses lwip sockets instead of `WiFiServer`/`WiFiClient`
- Uses ESP-IDF UART driver instead of `Serial2`
- Uses ESP-IDF GPIO driver instead of `pinMode()`/`digitalWrite()`

## Next Steps

- Enable camera initialization when driver issues are resolved
- Add esp-who for better face recognition (if needed)
- Customize provisioning UI (currently uses default ESP-IDF portal)

