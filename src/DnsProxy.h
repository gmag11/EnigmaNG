#ifndef MESH_DNS_PROXY_H
#define MESH_DNS_PROXY_H

#if !defined(ESP8266)

#include <Arduino.h>
#include <IPAddress.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <lwip/sockets.h>  // sockaddr, socklen_t

// ─── DNS wire-format constants ────────────────────────────────────────────────

#define DNS_PORT        53
#define DNS_MAX_PACKET  512
#define DNS_HEADER_LEN  12

// DNS RCODE values
#define DNS_RCODE_NOERROR   0
#define DNS_RCODE_SERVFAIL  2
#define DNS_RCODE_NXDOMAIN  3

// DNS record types / classes
#define DNS_TYPE_A   1
#define DNS_CLASS_IN 1

// ─── DNS cache ────────────────────────────────────────────────────────────────

#define DNS_CACHE_MAX_ENTRIES 64
#define DNS_CACHE_NAME_LEN    64
#define DNS_MIN_TTL_FLOOR_S   60   // minimum TTL floor (configurable)

struct DnsCacheEntry {
    uint32_t ip;                        // resolved IPv4 (host byte order)
    uint32_t expireMs;                  // millis() when entry expires
    uint32_t lastUseMs;                 // millis() of last cache hit (LRU)
    char     name[DNS_CACHE_NAME_LEN];  // hostname (NUL-terminated, lowercased)
};

class DnsCache {
public:
    explicit DnsCache(uint32_t minTtlFloorS = DNS_MIN_TTL_FLOOR_S)
        : _minTtlFloorS(minTtlFloorS) {}

    // Returns 0 if not found or expired; sets lastUseMs on hit.
    uint32_t lookup(const char* name);

    // Inserts or updates an entry. TTL is clamped to _minTtlFloorS if lower.
    void insert(const char* name, uint32_t ip, uint32_t ttlSeconds);

    // Removes all expired entries.
    void evictExpired();

    // Clears the entire cache.
    void flush();

    // Returns all current (non-expired) entries (for diagnostics).
    const std::vector<DnsCacheEntry>& entries() const { return _entries; }

    void setMinTtlFloor(uint32_t s) { _minTtlFloorS = s; }

private:
    std::vector<DnsCacheEntry> _entries;
    uint32_t _minTtlFloorS;

    // Returns index of the LRU entry (oldest lastUseMs).
    int _lruIndex() const;
};

// ─── Custom DNS records ───────────────────────────────────────────────────────

struct DnsRecord {
    char name[DNS_CACHE_NAME_LEN];  // hostname (NUL-terminated, stored lowercased)
    uint32_t ip;                    // IPv4 in host byte order
};

class DnsCustomRecords {
public:
    // Returns 0 if not found. Case-insensitive.
    uint32_t lookup(const char* name) const;

    // Adds or replaces a record.
    void add(const char* name, uint32_t ip);

    // Removes matching record (case-insensitive). Returns true if found.
    bool remove(const char* name);

    const std::vector<DnsRecord>& getAll() const { return _records; }

    // NVS persistence (namespace "dns", key "records")
    bool save() const;
    bool load();

private:
    std::vector<DnsRecord> _records;

    static void _toLower(char* dst, const char* src, size_t n);
};

// ─── DNS Proxy ────────────────────────────────────────────────────────────────

class DnsProxy {
public:
    // Start the DNS proxy server bound to meshIp:53.
    // Loads custom records from NVS and creates the server task.
    bool begin(IPAddress meshIp);

    // Stop the server task and close the socket.
    void stop();

    // Returns true if currently running.
    bool isRunning() const { return _running; }

    // Update upstream DNS IP (call when WiFi STA reconnects).
    void setUpstreamDns(uint32_t ip) { _upstreamIp = ip; }
    uint32_t getUpstreamDns() const  { return _upstreamIp; }

    // Flush DNS cache (called when custom records are saved).
    void flushCache() { _cache.flush(); }

    // Access to sub-objects for Web UI / API handlers.
    DnsCustomRecords& customRecords() { return _customRecords; }
    DnsCache&         cache()         { return _cache; }

    // Relay a DNS query to upstream DNS server. Returns false on timeout/error.
    // On success, fills responseBuf with the upstream response and sets responseLen.
    bool relayToUpstream(const uint8_t* queryPacket, size_t queryLen,
                         uint32_t upstreamIp,
                         uint8_t* responseBuf, size_t responseBufLen,
                         size_t& responseLen);

    // ── DNS packet helpers (public for unit tests) ──────────────────────────

    // Parse DNS question from packet. Returns false if not Type A / Class IN.
    // On success, fills name (max DNS_CACHE_NAME_LEN), txId, returns true.
    static bool parseQuery(const uint8_t* pkt, size_t len,
                           uint16_t& txId, char* name, size_t nameLen);

    // Build an A-record response into buf. Returns bytes written.
    static size_t buildAResponse(uint8_t* buf, size_t bufLen,
                                 uint16_t txId, const char* name,
                                 uint32_t ip, uint32_t ttl);

    // Build a SERVFAIL response into buf. Returns bytes written.
    static size_t buildServfail(uint8_t* buf, size_t bufLen,
                                uint16_t txId, const char* name);

    // Build a NOERROR / empty-answer response (for unsupported query types).
    static size_t buildNoerrorEmpty(uint8_t* buf, size_t bufLen,
                                    uint16_t txId, const char* name);

private:
    DnsCache         _cache;
    DnsCustomRecords _customRecords;
    int              _sock    = -1;
    volatile bool    _running = false;
    uint32_t         _upstreamIp = 0;

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(IDF_VER)
    TaskHandle_t     _taskHandle = nullptr;
    static void      _taskEntry(void* arg);
#endif

    void _runLoop();
    void _processQuery(const uint8_t* pkt, size_t len,
                       const struct sockaddr* client, socklen_t clientLen);

public:
    // Encode/decode DNS name labels (wire format ↔ ASCII dotted).
    // Public so that helper free functions in the .cpp can call them.
    static int  _encodeName(uint8_t* out, size_t outLen, const char* name);
    static bool _decodeName(const uint8_t* pkt, size_t pktLen,
                            size_t offset, char* out, size_t outLen);
};

// Helper: encode an IPv4 address string "a.b.c.d" to uint32_t (host order).
// Returns 0 on parse error.
uint32_t dnsParseIp(const char* str);

#endif // !ESP8266
#endif // MESH_DNS_PROXY_H
