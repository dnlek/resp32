# ESP32-S3 Camera Server

ESP-IDF project for ESP32-S3 with camera, WiFi provisioning over BLE, and HTTP server.

## Features

- Camera support (M5STACK_WIDE pinout, OV3660 sensor)
- WiFi provisioning over Bluetooth Low Energy
- HTTP server with `/status` and `/cam` endpoints
- Auto-reconnect on WiFi disconnect

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Endpoints

- `GET /status` - JSON status (camera state, WiFi connection)
- `GET /cam` - MJPEG camera stream

## WiFi Provisioning

On first boot, device starts BLE provisioning. Use ESP BLE Provisioning app to configure WiFi credentials.

