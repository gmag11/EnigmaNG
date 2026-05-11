#pragma once
// lwip/dns.h stub for native unit test builds.

#include <stdint.h>
#include <string.h>

// Minimal ip_addr_t needed for dns_setserver
#ifndef LWIP_IP_ADDR_T_DEFINED
#define LWIP_IP_ADDR_T_DEFINED
typedef struct {
    uint32_t addr;
} ip_addr_t;
#endif

#define DNS_MAX_SERVERS 2

// Track dns_setserver calls for unit test assertions
struct DnsSetserverStubState {
    uint8_t   lastIndex = 0;
    ip_addr_t lastAddr  = {};
    int       callCount = 0;
};

inline DnsSetserverStubState& dnsSetserverStubState() {
    static DnsSetserverStubState s;
    return s;
}

inline void dns_setserver(uint8_t numdns, const ip_addr_t* dnsserver) {
    auto& s = dnsSetserverStubState();
    s.lastIndex = numdns;
    if (dnsserver) s.lastAddr = *dnsserver;
    else memset(&s.lastAddr, 0, sizeof(s.lastAddr));
    ++s.callCount;
}
