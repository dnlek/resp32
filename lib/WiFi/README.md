# WiFi Library Overrides

This directory contains local overrides of the Arduino ESP32 WiFi library files to fix compatibility issues with the latest ESP-IDF.

## Files Modified

- **WiFiGeneric.cpp**: DNS functions use correct `esp_ip_addr_t` structure (`u_addr.ip4.addr`)
- **WiFiSTA.cpp**: IPv6 functions disabled (not available in current ESP-IDF config)
- **WiFiAP.cpp**: IPv6 functions disabled (not available in current ESP-IDF config)

## Changes Made

### IPv6 Functions Disabled
- `WiFiSTAClass::enableIpV6()` - Returns `false`
- `WiFiSTAClass::localIPv6()` - Returns empty `IPv6Address()`
- `WiFiAPClass::softAPenableIpV6()` - Returns `false`
- `WiFiAPClass::softAPIPv6()` - Returns empty `IPv6Address()`

### Structure Access
- `ip_addr_t` (LWIP): Uses `.addr` directly
- `esp_ip_addr_t` (ESP-IDF DNS): Uses `.u_addr.ip4.addr`

## Why Local Overrides?

Instead of patching the framework library files (which get overwritten on updates), we use PlatformIO's library override mechanism. Files in `lib/` take precedence over framework libraries, ensuring our fixes persist.

## Maintenance

If the Arduino ESP32 framework is updated and new WiFi library features are added, you may need to:
1. Copy the new WiFi library files from the framework
2. Re-apply the compatibility fixes
3. Test to ensure everything still works

