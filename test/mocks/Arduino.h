#pragma once
// Mock Arduino.h for native (PC) unit test builds.
// Provides the minimal subset used by the portable library sources.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <chrono>
#include <string>

// ──────────────────────────────────────────────────────────────────────────────
// Arduino String class (wraps std::string, adds numeric operators)
// ──────────────────────────────────────────────────────────────────────────────
class String {
public:
    std::string _s;
    String()                 = default;
    String(const char* s)    : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(int n)            : _s(std::to_string(n)) {}
    String(unsigned int n)   : _s(std::to_string(n)) {}
    String(long n)           : _s(std::to_string(n)) {}
    String(uint8_t n)        : _s(std::to_string(n)) {}
    String operator+(const String& o) const { return String(_s + o._s); }
    String operator+(const char* s)   const { return String(_s + (s?s:"")); }
    template<typename T> String operator+(T n) const { return String(_s + std::to_string(n)); }
    String& operator+=(const String& o) { _s += o._s; return *this; }
    const char* c_str()  const { return _s.c_str(); }
    size_t length()      const { return _s.length(); }
    bool   isEmpty()     const { return _s.empty(); }
};
inline String operator+(const char* lhs, const String& rhs) { return String(std::string(lhs) + rhs._s); }

// ──────────────────────────────────────────────────────────────────────────────
// Time
// ──────────────────────────────────────────────────────────────────────────────
inline uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

inline void delay(uint32_t) {}

// ──────────────────────────────────────────────────────────────────────────────
// Math helpers
// ──────────────────────────────────────────────────────────────────────────────
// constrain: allow first arg to differ in type from bounds (e.g. long vs int)
template<typename T, typename L, typename H>
inline T constrain(T val, L lo, H hi) {
    if (val < (T)lo) return (T)lo;
    if (val > (T)hi) return (T)hi;
    return val;
}

// map: all args may differ in type
template<typename A, typename B, typename C, typename D, typename E>
inline long map(A x, B in_min, C in_max, D out_min, E out_max) {
    return (long)((long)x - (long)in_min)
         * ((long)out_max - (long)out_min)
         / ((long)in_max  - (long)in_min)
         + (long)out_min;
}

// ──────────────────────────────────────────────────────────────────────────────
// Misc macros
// ──────────────────────────────────────────────────────────────────────────────
#define PROGMEM
#define RTC_DATA_ATTR

// ──────────────────────────────────────────────────────────────────────────────
// Serial
// ──────────────────────────────────────────────────────────────────────────────
struct SerialClass {
    template<typename T> void print(T)    {}
    template<typename T> void println(T)  {}
    void println()                        {}
    void begin(uint32_t)                  {}
    // printf va_args stub
    void printf(const char*, ...) { }
};
inline SerialClass Serial;

// ──────────────────────────────────────────────────────────────────────────────
// ESP chip object
// ──────────────────────────────────────────────────────────────────────────────
struct EspClass {
    uint32_t getFreeHeap()     { return 100000; }
    uint32_t getHeapSize()     { return 320000; }
    uint32_t getCpuFreqMHz()   { return 240; }
    uint32_t getFlashChipSize(){ return 4194304; }
    uint8_t  getChipRevision() { return 0; }
};
inline EspClass ESP;


