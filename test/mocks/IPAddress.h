#pragma once
// Minimal IPAddress stub for native unit test builds.
#include <stdint.h>
#include <stdio.h>
#include <string>

class IPAddress {
    uint32_t _addr;
public:
    IPAddress() : _addr(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : _addr(((uint32_t)a) | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24)) {}
    explicit IPAddress(uint32_t addr) : _addr(addr) {}
    bool operator==(const IPAddress& o) const { return _addr == o._addr; }
    bool operator!=(const IPAddress& o) const { return _addr != o._addr; }
    operator uint32_t() const { return _addr; }
    std::string toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 (unsigned)(_addr & 0xFF),
                 (unsigned)((_addr >> 8) & 0xFF),
                 (unsigned)((_addr >> 16) & 0xFF),
                 (unsigned)((_addr >> 24) & 0xFF));
        return buf;
    }
};
