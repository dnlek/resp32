#ifndef LWIP_COMPAT_H
#define LWIP_COMPAT_H

// Compatibility header for WiFi library with newer ESP-IDF versions
// Fixes u_addr.ip4.addr -> addr structure access

#include <lwip/ip_addr.h>

// For IPv4, ip_addr_t is just ip4_addr which has 'addr' directly
// The old code uses u_addr.ip4.addr, but newer ESP-IDF uses addr directly
#if LWIP_IPV4 && !LWIP_IPV6
// When only IPv4 is enabled, ip_addr_t is ip4_addr
#define IPADDR_TO_U32(ipaddr) ((ipaddr)->addr)
#else
// When IPv6 is enabled, we need to check the type
// This is a compatibility macro for the WiFi library
#define IPADDR_TO_U32(ipaddr) ((ipaddr)->u_addr.ip4.addr)
#endif

// Compatibility macro for accessing IPv4 address
#ifndef IP4_ADDR_VAL
#define IP4_ADDR_VAL(ipaddr) ((ipaddr)->addr)
#endif

#endif // LWIP_COMPAT_H

