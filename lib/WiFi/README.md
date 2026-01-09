# WiFi Library Overrides

This directory contains local overrides of the Arduino ESP32 WiFi library files to fix compatibility issues with ESP-IDF and the Arduino ESP32 framework.

## Can I Use OOTB WiFi Library?

**No, the local WiFi library is required.** Testing showed that the out-of-the-box (OOTB) WiFi library causes crashes (`StoreProhibited`) with this ESP-IDF configuration. The local library includes necessary compatibility fixes.

## Files Modified

- **WiFiGeneric.cpp**: DNS functions use correct `esp_ip_addr_t` structure (`u_addr.ip4.addr`)
- **WiFiSTA.cpp**: DNS functions use correct `ip_addr_t` structure (`u_addr.ip4.addr`) and IPv6 functions disabled
- **WiFiAP.cpp**: IPv6 functions disabled (not available in current ESP-IDF config)

## Changes Made

### DNS Structure Access Fixes
- **WiFiGeneric.cpp**: `wifi_dns_found_callback()` - Uses `ipaddr->u_addr.ip4.addr` instead of `ipaddr->addr`
- **WiFiGeneric.cpp**: `hostByName()` - Uses `addr.u_addr.ip4.addr` instead of `addr.addr`
- **WiFiSTA.cpp**: `dnsIP()` - Uses `dns_ip->u_addr.ip4.addr` instead of `dns_ip->addr`

These fixes are required when using the framework's default `sdkconfig.h` which has IPv6 enabled. The LWIP `ip_addr_t` structure requires `u_addr.ip4.addr` access when IPv6 support is compiled in.

### IPv6 Functions Disabled
- `WiFiSTAClass::enableIpV6()` - Returns `false`
- `WiFiSTAClass::localIPv6()` - Returns empty `IPv6Address()`
- `WiFiAPClass::softAPenableIpV6()` - Returns `false`
- `WiFiAPClass::softAPIPv6()` - Returns empty `IPv6Address()`

### Structure Access
- `ip_addr_t` (LWIP with IPv6): Uses `.u_addr.ip4.addr`
- `esp_ip_addr_t` (ESP-IDF DNS): Uses `.u_addr.ip4.addr`

## Why Local Overrides?

Instead of patching the framework library files (which get overwritten on updates), we use PlatformIO's library override mechanism. Files in `lib/` take precedence over framework libraries, ensuring our fixes persist.

## Configuration

- **Framework**: Arduino ESP32 (espressif32 @ 6.12.0, arduinoespressif32 @ 3.20017.241212)
- **Target**: ESP32-S3
- **sdkconfig.h**: Using framework defaults (IPv6 enabled)

## Maintenance

If the Arduino ESP32 framework is updated and new WiFi library features are added, you may need to:
1. Copy the new WiFi library files from the framework
2. Re-apply the compatibility fixes (especially DNS structure access)
3. Test to ensure everything still works

