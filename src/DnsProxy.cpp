#if !defined(ESP8266)

#include "DnsProxy.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include <lwip/sockets.h>
#include <fcntl.h>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <algorithm>

// ─── Platform-specific includes ──────────────────────────────────────────────

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(IDF_VER)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static void str_to_lower(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n - 1 && src[i]; ++i) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// DnsCache
// ─────────────────────────────────────────────────────────────────────────────

uint32_t DnsCache::lookup(const char* name) {
    char key[DNS_CACHE_NAME_LEN];
    str_to_lower(key, name, sizeof(key));

    uint32_t now = millis();
    for (auto& e : _entries) {
        if (strncmp(e.name, key, DNS_CACHE_NAME_LEN) == 0) {
            if ((int32_t)(now - e.expireMs) >= 0) {
                // Expired — treat as miss, will be cleaned by evictExpired
                return 0;
            }
            e.lastUseMs = now;
            return e.ip;
        }
    }
    return 0;
}

void DnsCache::insert(const char* name, uint32_t ip, uint32_t ttlSeconds) {
    char key[DNS_CACHE_NAME_LEN];
    str_to_lower(key, name, sizeof(key));

    uint32_t effectiveTtl = (ttlSeconds < _minTtlFloorS) ? _minTtlFloorS : ttlSeconds;
    uint32_t now = millis();
    uint32_t expireMs = now + effectiveTtl * 1000u;

    // Update existing entry if present
    for (auto& e : _entries) {
        if (strncmp(e.name, key, DNS_CACHE_NAME_LEN) == 0) {
            e.ip       = ip;
            e.expireMs = expireMs;
            e.lastUseMs = now;
            return;
        }
    }

    // Evict expired entries first
    evictExpired();

    // If still full, evict LRU
    if ((int)_entries.size() >= DNS_CACHE_MAX_ENTRIES) {
        int idx = _lruIndex();
        if (idx >= 0) _entries.erase(_entries.begin() + idx);
    }

    DnsCacheEntry e;
    str_to_lower(e.name, name, sizeof(e.name));
    e.ip       = ip;
    e.expireMs = expireMs;
    e.lastUseMs = now;
    _entries.push_back(e);
}

void DnsCache::evictExpired() {
    uint32_t now = millis();
    _entries.erase(
        std::remove_if(_entries.begin(), _entries.end(),
                       [now](const DnsCacheEntry& e) {
                           return (int32_t)(now - e.expireMs) >= 0;
                       }),
        _entries.end());
}

void DnsCache::flush() {
    _entries.clear();
}

int DnsCache::_lruIndex() const {
    if (_entries.empty()) return -1;
    int idx = 0;
    for (int i = 1; i < (int)_entries.size(); ++i) {
        if ((int32_t)(_entries[i].lastUseMs - _entries[idx].lastUseMs) < 0)
            idx = i;
    }
    return idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// DnsCustomRecords
// ─────────────────────────────────────────────────────────────────────────────

void DnsCustomRecords::_toLower(char* dst, const char* src, size_t n) {
    str_to_lower(dst, src, n);
}

uint32_t DnsCustomRecords::lookup(const char* name) const {
    char key[DNS_CACHE_NAME_LEN];
    str_to_lower(key, name, sizeof(key));
    for (const auto& r : _records) {
        if (strncmp(r.name, key, DNS_CACHE_NAME_LEN) == 0) return r.ip;
    }
    return 0;
}

void DnsCustomRecords::add(const char* name, uint32_t ip) {
    char key[DNS_CACHE_NAME_LEN];
    str_to_lower(key, name, sizeof(key));
    for (auto& r : _records) {
        if (strncmp(r.name, key, DNS_CACHE_NAME_LEN) == 0) {
            r.ip = ip;
            return;
        }
    }
    DnsRecord rec;
    str_to_lower(rec.name, name, sizeof(rec.name));
    rec.ip = ip;
    _records.push_back(rec);
}

bool DnsCustomRecords::remove(const char* name) {
    char key[DNS_CACHE_NAME_LEN];
    str_to_lower(key, name, sizeof(key));
    for (auto it = _records.begin(); it != _records.end(); ++it) {
        if (strncmp(it->name, key, DNS_CACHE_NAME_LEN) == 0) {
            _records.erase(it);
            return true;
        }
    }
    return false;
}

// ── NVS persistence (minimal JSON: [{"name":"...","ip":"..."},...]) ──────────

bool DnsCustomRecords::save() const {
    // Build JSON string
    std::string json = "[";
    for (size_t i = 0; i < _records.size(); ++i) {
        if (i > 0) json += ",";
        uint8_t a = (_records[i].ip >> 24) & 0xFF;
        uint8_t b = (_records[i].ip >> 16) & 0xFF;
        uint8_t c = (_records[i].ip >> 8)  & 0xFF;
        uint8_t d = (_records[i].ip)       & 0xFF;
        char ipStr[20];
        snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", a, b, c, d);
        json += "{\"name\":\"";
        json += _records[i].name;
        json += "\",\"ip\":\"";
        json += ipStr;
        json += "\"}";
    }
    json += "]";

    nvs_handle_t h;
    if (nvs_open("dns", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, "records", json.c_str());
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool DnsCustomRecords::load() {
    _records.clear();

    nvs_handle_t h;
    if (nvs_open("dns", NVS_READONLY, &h) != ESP_OK) return true; // no namespace yet → empty

    size_t needed = 0;
    esp_err_t err = nvs_get_str(h, "records", nullptr, &needed);
    if (err != ESP_OK || needed == 0) {
        nvs_close(h);
        return true; // no key yet → empty list
    }

    std::string buf(needed, '\0');
    err = nvs_get_str(h, "records", &buf[0], &needed);
    nvs_close(h);
    if (err != ESP_OK) return true;

    // Minimal JSON parser for: [{"name":"...","ip":"..."},...}]
    // Uses simple string scanning — no recursive descent needed for this format.
    const char* p = buf.c_str();
    while (*p) {
        // Find next {"name":"
        const char* nameStart = strstr(p, "\"name\":\"");
        if (!nameStart) break;
        nameStart += 8; // skip past "name":"
        const char* nameEnd = strchr(nameStart, '"');
        if (!nameEnd) { Serial.println("[DNS] JSON parse error: unclosed name"); _records.clear(); return false; }

        const char* ipStart = strstr(nameEnd, "\"ip\":\"");
        if (!ipStart) { Serial.println("[DNS] JSON parse error: missing ip field"); _records.clear(); return false; }
        ipStart += 6; // skip past "ip":"
        const char* ipEnd = strchr(ipStart, '"');
        if (!ipEnd) { Serial.println("[DNS] JSON parse error: unclosed ip"); _records.clear(); return false; }

        // Extract name and ip strings
        size_t nameLen = (size_t)(nameEnd - nameStart);
        size_t ipLen   = (size_t)(ipEnd - ipStart);
        if (nameLen == 0 || nameLen >= DNS_CACHE_NAME_LEN || ipLen >= 16) {
            p = ipEnd + 1;
            continue;
        }

        char nameStr[DNS_CACHE_NAME_LEN] = {};
        char ipStr[16] = {};
        memcpy(nameStr, nameStart, nameLen);
        memcpy(ipStr, ipStart, ipLen);

        uint32_t ip = dnsParseIp(ipStr);
        if (ip != 0) add(nameStr, ip);

        p = ipEnd + 1;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DnsProxy — packet helpers
// ─────────────────────────────────────────────────────────────────────────────

// Encode dotted-decimal hostname to DNS label format.
// e.g. "www.example.com" → \x03www\x07example\x03com\x00
// Returns number of bytes written, or -1 on error.
int DnsProxy::_encodeName(uint8_t* out, size_t outLen, const char* name) {
    size_t pos = 0;
    const char* p = name;
    while (true) {
        const char* dot = strchr(p, '.');
        size_t labelLen = dot ? (size_t)(dot - p) : strlen(p);
        if (labelLen > 63 || pos + 1 + labelLen + 1 > outLen) return -1;
        out[pos++] = (uint8_t)labelLen;
        memcpy(out + pos, p, labelLen);
        pos += labelLen;
        if (!dot) break;
        p = dot + 1;
    }
    if (pos >= outLen) return -1;
    out[pos++] = 0; // root label
    return (int)pos;
}

// Decode DNS wire-format name to dotted-decimal string.
// Handles pointer compression (RFC 1035 §4.1.4).
// Returns true on success.
bool DnsProxy::_decodeName(const uint8_t* pkt, size_t pktLen,
                            size_t offset, char* out, size_t outLen) {
    size_t outPos = 0;
    bool jumped = false;
    size_t safetyCounter = 0;

    while (offset < pktLen) {
        if (++safetyCounter > 256) return false; // loop guard
        uint8_t len = pkt[offset];

        if (len == 0) break;  // end of name

        if ((len & 0xC0) == 0xC0) {
            // Pointer compression
            if (offset + 1 >= pktLen) return false;
            size_t ptr = ((len & 0x3F) << 8) | pkt[offset + 1];
            if (!jumped) offset += 2; // don't advance past pointer on return
            offset = ptr;
            jumped = true;
            continue;
        }

        offset++;
        if (offset + len > pktLen) return false;

        if (outPos > 0) {
            if (outPos + 1 >= outLen) return false;
            out[outPos++] = '.';
        }
        if (outPos + len >= outLen) return false;
        memcpy(out + outPos, pkt + offset, len);
        outPos += len;
        offset += len;
    }
    if (outPos < outLen) out[outPos] = '\0';
    else return false;
    return true;
}

bool DnsProxy::parseQuery(const uint8_t* pkt, size_t len,
                          uint16_t& txId, char* name, size_t nameLen) {
    if (!pkt || len < DNS_HEADER_LEN + 2) return false;

    // DNS header (RFC 1035 §4.1.1)
    txId = ((uint16_t)pkt[0] << 8) | pkt[1];
    // uint16_t flags = ((uint16_t)pkt[2] << 8) | pkt[3];
    uint16_t qdCount = ((uint16_t)pkt[4] << 8) | pkt[5];
    if (qdCount < 1) return false;

    // Parse first question
    if (!_decodeName(pkt, len, DNS_HEADER_LEN, name, nameLen)) return false;

    // Find end of name in wire format to get QTYPE/QCLASS
    size_t nameEnd = DNS_HEADER_LEN;
    size_t guard = 0;
    while (nameEnd < len && ++guard < 256) {
        uint8_t labelLen = pkt[nameEnd];
        if (labelLen == 0) { nameEnd++; break; }
        if ((labelLen & 0xC0) == 0xC0) { nameEnd += 2; break; }
        nameEnd += 1 + labelLen;
    }
    if (nameEnd + 4 > len) return false;

    uint16_t qtype  = ((uint16_t)pkt[nameEnd]     << 8) | pkt[nameEnd + 1];
    uint16_t qclass = ((uint16_t)pkt[nameEnd + 2] << 8) | pkt[nameEnd + 3];

    return (qtype == DNS_TYPE_A && qclass == DNS_CLASS_IN);
}

// Build DNS response header (common to A / SERVFAIL / NOERROR-empty).
// flags: e.g. 0x8180 (standard response, recursion available)
static size_t _buildHeader(uint8_t* buf, size_t bufLen,
                           uint16_t txId, uint16_t flags,
                           uint16_t qdCount, uint16_t anCount) {
    if (bufLen < DNS_HEADER_LEN) return 0;
    buf[0] = (uint8_t)(txId >> 8);   buf[1] = (uint8_t)txId;
    buf[2] = (uint8_t)(flags >> 8);  buf[3] = (uint8_t)flags;
    buf[4] = (uint8_t)(qdCount >> 8); buf[5] = (uint8_t)qdCount;
    buf[6] = (uint8_t)(anCount >> 8); buf[7] = (uint8_t)anCount;
    buf[8] = 0; buf[9] = 0;   // NS count
    buf[10] = 0; buf[11] = 0; // AR count
    return DNS_HEADER_LEN;
}

// Append the question section for a given name (replicating original query).
static size_t _appendQuestion(uint8_t* buf, size_t bufLen, size_t pos,
                              const char* name) {
    int nameBytes = DnsProxy::_encodeName(buf + pos, bufLen - pos, name);
    if (nameBytes <= 0) return 0;
    pos += (size_t)nameBytes;
    if (pos + 4 > bufLen) return 0;
    buf[pos++] = 0; buf[pos++] = DNS_TYPE_A;    // QTYPE A
    buf[pos++] = 0; buf[pos++] = DNS_CLASS_IN;  // QCLASS IN
    return pos;
}

size_t DnsProxy::buildAResponse(uint8_t* buf, size_t bufLen,
                                uint16_t txId, const char* name,
                                uint32_t ip, uint32_t ttl) {
    // Flags: QR=1, OPCODE=0, AA=0, TC=0, RD=1, RA=1, RCODE=NOERROR
    size_t pos = _buildHeader(buf, bufLen, txId, 0x8180, 1, 1);
    if (!pos) return 0;

    pos = _appendQuestion(buf, bufLen, pos, name);
    if (!pos) return 0;

    // Answer RR: use pointer to question name (offset 12)
    if (pos + 2 + 2 + 2 + 4 + 2 + 4 > bufLen) return 0;
    buf[pos++] = 0xC0; buf[pos++] = DNS_HEADER_LEN; // name pointer to offset 12
    buf[pos++] = 0; buf[pos++] = DNS_TYPE_A;        // TYPE A
    buf[pos++] = 0; buf[pos++] = DNS_CLASS_IN;      // CLASS IN
    buf[pos++] = (uint8_t)(ttl >> 24);
    buf[pos++] = (uint8_t)(ttl >> 16);
    buf[pos++] = (uint8_t)(ttl >> 8);
    buf[pos++] = (uint8_t)ttl;
    buf[pos++] = 0; buf[pos++] = 4; // RDLENGTH = 4
    buf[pos++] = (uint8_t)(ip >> 24);
    buf[pos++] = (uint8_t)(ip >> 16);
    buf[pos++] = (uint8_t)(ip >> 8);
    buf[pos++] = (uint8_t)ip;
    return pos;
}

size_t DnsProxy::buildServfail(uint8_t* buf, size_t bufLen,
                               uint16_t txId, const char* name) {
    // Flags: QR=1, RD=1, RA=1, RCODE=SERVFAIL(2)
    size_t pos = _buildHeader(buf, bufLen, txId, 0x8182, 1, 0);
    if (!pos) return 0;
    pos = _appendQuestion(buf, bufLen, pos, name);
    return pos;
}

size_t DnsProxy::buildNoerrorEmpty(uint8_t* buf, size_t bufLen,
                                   uint16_t txId, const char* name) {
    // Flags: QR=1, RD=1, RA=1, RCODE=NOERROR(0), no answers
    size_t pos = _buildHeader(buf, bufLen, txId, 0x8180, 1, 0);
    if (!pos) return 0;
    pos = _appendQuestion(buf, bufLen, pos, name);
    return pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// DnsProxy — upstream relay
// ─────────────────────────────────────────────────────────────────────────────

bool DnsProxy::relayToUpstream(const uint8_t* queryPacket, size_t queryLen,
                               uint32_t upstreamIp,
                               uint8_t* responseBuf, size_t responseBufLen,
                               size_t& responseLen) {
    responseLen = 0;
    if (!queryPacket || queryLen == 0 || upstreamIp == 0) return false;

    IPAddress up(upstreamIp >> 24, (upstreamIp >> 16) & 0xFF,
                 (upstreamIp >> 8) & 0xFF, upstreamIp & 0xFF);
    Serial.printf("[DNS] relay → upstream %s\n", up.toString().c_str());

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        Serial.printf("[DNS] relay: socket() failed errno=%d\n", errno);
        return false;
    }
    fcntl(sock, F_SETFL, O_NONBLOCK);

    struct sockaddr_in upstream = {};
    upstream.sin_family = AF_INET;
    upstream.sin_port   = htons(DNS_PORT);
    upstream.sin_addr.s_addr = htonl(upstreamIp);

    ssize_t sent = sendto(sock, queryPacket, queryLen, 0,
                          (struct sockaddr*)&upstream, sizeof(upstream));
    if (sent < 0 || (size_t)sent != queryLen) {
        Serial.printf("[DNS] relay: sendto() failed errno=%d\n", errno);
        close(sock);
        return false;
    }

    // Wait up to 2 seconds for a response
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    int ready = select(sock + 1, &fds, nullptr, nullptr, &tv);
    if (ready <= 0) {
        Serial.println("[DNS] relay: timeout waiting for upstream response");
        close(sock);
        return false;
    }

    struct sockaddr_in from = {};
    socklen_t fromLen = sizeof(from);
    ssize_t rcvd = recvfrom(sock, responseBuf, responseBufLen, 0,
                             (struct sockaddr*)&from, &fromLen);
    close(sock);
    if (rcvd <= 0) {
        Serial.printf("[DNS] relay: recvfrom() failed errno=%d\n", errno);
        return false;
    }

    responseLen = (size_t)rcvd;
    Serial.printf("[DNS] relay: got %d byte response\n", (int)rcvd);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DnsProxy — server task and main loop
// ─────────────────────────────────────────────────────────────────────────────

bool DnsProxy::begin(IPAddress meshIp) {
    if (_running) return true;

    // Load NVS records
    _customRecords.load();

    // Get upstream DNS from WiFi STA netif
    esp_netif_t* staNif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (staNif) {
        esp_netif_dns_info_t dnsInfo = {};
        if (esp_netif_get_dns_info(staNif, ESP_NETIF_DNS_MAIN, &dnsInfo) == ESP_OK) {
            _upstreamIp = ntohl(dnsInfo.ip.u_addr.ip4.addr);
        }
    }
    if (_upstreamIp) {
        IPAddress up(_upstreamIp >> 24, (_upstreamIp >> 16) & 0xFF,
                     (_upstreamIp >> 8) & 0xFF, _upstreamIp & 0xFF);
        Serial.printf("[DNS] Upstream DNS: %s\n", up.toString().c_str());
    } else {
        Serial.println("[DNS] WARNING: upstream DNS not available yet (WiFi uplink not connected)");
    }

    // Create UDP socket using POSIX API (not lwip_socket directly) so that
    // ESP-IDF's VFS layer routes packets correctly under IP_NAPT — this matches
    // the approach used by EthWiFiManager's working DNS proxy.
    _sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_sock < 0) {
        Serial.println("[DNS] ERROR: failed to create socket");
        return false;
    }

    // SO_REUSEADDR: allow rapid restart without "address already in use"
    int reuseFlag = 1;
    setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, &reuseFlag, sizeof(reuseFlag));

    // O_NONBLOCK: use select() in the loop rather than blocking recvfrom()
    fcntl(_sock, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    Serial.printf("[DNS] Binding socket fd=%d to INADDR_ANY:%d\n", _sock, DNS_PORT);

    if (bind(_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Serial.printf("[DNS] ERROR: bind(INADDR_ANY) failed errno=%d\n", errno);
        close(_sock);
        _sock = -1;
        return false;
    }

    Serial.printf("[DNS] Socket bound (fd=%d)\n", _sock);

    _running = true;

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(IDF_VER)
    xTaskCreate(_taskEntry, "xDnsTask", 3072, this, 4, &_taskHandle);
#endif

    Serial.printf("[DNS] Proxy started on %s:53\n", meshIp.toString().c_str());
    return true;
}

void DnsProxy::stop() {
    if (!_running) return;
    _running = false;

    // Close socket to unblock select()
    if (_sock >= 0) {
        close(_sock);
        _sock = -1;
    }

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(IDF_VER)
    if (_taskHandle) {
        // Give the task a moment to exit, then delete it.
        vTaskDelay(pdMS_TO_TICKS(200));
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
#endif
    Serial.println("[DNS] Proxy stopped");
}

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(IDF_VER)
void DnsProxy::_taskEntry(void* arg) {
    static_cast<DnsProxy*>(arg)->_runLoop();
    vTaskDelete(nullptr);
}
#endif

void DnsProxy::_runLoop() {
    uint8_t pkt[DNS_MAX_PACKET];
    struct sockaddr_in client;
    socklen_t clientLen = sizeof(client);

    Serial.printf("[DNS] _runLoop started (sock=%d, running=%d)\n", _sock, (int)_running);

    uint32_t loopCount = 0;
    while (_running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(_sock, &readSet);
        struct timeval tv = { 5, 0 };  // 5s timeout for alive log
        int ready = select(_sock + 1, &readSet, nullptr, nullptr, &tv);

        if (!_running) break;

        if (ready <= 0) {
            // Timeout — print alive marker
            if (++loopCount % 1 == 0) {
                Serial.printf("[DNS] task alive (waits=%lu, no pkts)\n",
                              (unsigned long)loopCount);
            }
            continue;
        }

        ssize_t len = recvfrom(_sock, pkt, sizeof(pkt), 0,
                               (struct sockaddr*)&client, &clientLen);
        if (len <= 0) continue;

        char srcIp[20] = "?";
        IPAddress(ntohl(client.sin_addr.s_addr)).toString().toCharArray(srcIp, sizeof(srcIp));
        Serial.printf("[DNS] pkt %d bytes from %s:%d\n",
                      (int)len, srcIp, ntohs(client.sin_port));

        _processQuery(pkt, (size_t)len, (struct sockaddr*)&client, clientLen);
    }
}

void DnsProxy::_processQuery(const uint8_t* pkt, size_t len,
                              const struct sockaddr* client, socklen_t clientLen) {
    uint16_t txId = 0;
    char name[DNS_CACHE_NAME_LEN] = {};

    // Log client IP
    char clientIpStr[20] = "?";
    if (client) {
        const struct sockaddr_in* sa = (const struct sockaddr_in*)client;
        IPAddress cip(ntohl(sa->sin_addr.s_addr));
        cip.toString().toCharArray(clientIpStr, sizeof(clientIpStr));
    }

    // Parse query — non-A/IN queries get NOERROR empty answer
    if (!parseQuery(pkt, len, txId, name, sizeof(name))) {
        // Re-extract txId for the error response
        if (len >= 2) txId = ((uint16_t)pkt[0] << 8) | pkt[1];
        // Try to decode name anyway for the response question section
        if (len >= DNS_HEADER_LEN) {
            _decodeName(pkt, len, DNS_HEADER_LEN, name, sizeof(name));
        }
        Serial.printf("[DNS] query from %s: '%s' (non-A/IN) → NOERROR empty\n", clientIpStr, name);
        uint8_t resp[DNS_MAX_PACKET];
        size_t respLen = buildNoerrorEmpty(resp, sizeof(resp), txId, name);
        if (respLen > 0) sendto(_sock, resp, respLen, 0, client, clientLen);
        return;
    }

    Serial.printf("[DNS] query from %s: '%s'\n", clientIpStr, name);

    // If upstream DNS not yet known, try to refresh it from the STA netif now
    if (_upstreamIp == 0) {
        esp_netif_t* staNif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (staNif) {
            esp_netif_dns_info_t dnsInfo = {};
            if (esp_netif_get_dns_info(staNif, ESP_NETIF_DNS_MAIN, &dnsInfo) == ESP_OK) {
                _upstreamIp = ntohl(dnsInfo.ip.u_addr.ip4.addr);
                if (_upstreamIp) {
                    IPAddress up(_upstreamIp >> 24, (_upstreamIp >> 16) & 0xFF,
                                 (_upstreamIp >> 8) & 0xFF, _upstreamIp & 0xFF);
                    Serial.printf("[DNS] upstream DNS refreshed: %s\n", up.toString().c_str());
                }
            }
        }
    }

    // Lookup priority: custom records → cache → upstream relay
    uint8_t resp[DNS_MAX_PACKET];
    size_t  respLen = 0;

    uint32_t ip = _customRecords.lookup(name);
    if (ip) {
        // Custom record match — respond immediately, no upstream
        IPAddress resolved(ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        Serial.printf("[DNS] '%s' → %s (custom record)\n", name, resolved.toString().c_str());
        respLen = buildAResponse(resp, sizeof(resp), txId, name, ip, 3600);
    } else {
        ip = _cache.lookup(name);
        if (ip) {
            // Cache hit
            IPAddress resolved(ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
            Serial.printf("[DNS] '%s' → %s (cache hit)\n", name, resolved.toString().c_str());
            respLen = buildAResponse(resp, sizeof(resp), txId, name, ip, DNS_MIN_TTL_FLOOR_S);
        } else {
            // Relay to upstream
            if (_upstreamIp == 0) {
                Serial.printf("[DNS] '%s' → SERVFAIL (no upstream DNS configured)\n", name);
            }
            uint8_t upstreamResp[DNS_MAX_PACKET];
            size_t  upstreamLen = 0;
            if (_upstreamIp && relayToUpstream(pkt, len, _upstreamIp,
                                               upstreamResp, sizeof(upstreamResp),
                                               upstreamLen)) {
                // Forward upstream response directly to client
                sendto(_sock, upstreamResp, upstreamLen, 0, client, clientLen);

                // Parse first A record from upstream response for logging + caching
                if (upstreamLen >= DNS_HEADER_LEN + 2) {
                    uint16_t anCount = ((uint16_t)upstreamResp[6] << 8) | upstreamResp[7];
                    if (anCount > 0) {
                        // Find end of question section
                        size_t pos = DNS_HEADER_LEN;
                        uint16_t qdCount = ((uint16_t)upstreamResp[4] << 8) | upstreamResp[5];
                        for (uint16_t q = 0; q < qdCount && pos < upstreamLen; ++q) {
                            // Skip name
                            size_t guard = 0;
                            while (pos < upstreamLen && ++guard < 256) {
                                uint8_t l = upstreamResp[pos];
                                if (l == 0) { pos++; break; }
                                if ((l & 0xC0) == 0xC0) { pos += 2; break; }
                                pos += 1 + l;
                            }
                            pos += 4; // skip QTYPE + QCLASS
                        }
                        // Parse first answer RR
                        if (pos + 10 + 4 <= upstreamLen) {
                            // Skip name
                            size_t guard = 0;
                            size_t rrStart = pos;
                            while (pos < upstreamLen && ++guard < 256) {
                                uint8_t l = upstreamResp[pos];
                                if (l == 0) { pos++; break; }
                                if ((l & 0xC0) == 0xC0) { pos += 2; break; }
                                pos += 1 + l;
                            }
                            (void)rrStart;
                            if (pos + 10 <= upstreamLen) {
                                uint16_t rtype  = ((uint16_t)upstreamResp[pos]   << 8) | upstreamResp[pos+1];
                                uint16_t rclass = ((uint16_t)upstreamResp[pos+2] << 8) | upstreamResp[pos+3];
                                uint32_t rttl   = ((uint32_t)upstreamResp[pos+4] << 24) |
                                                  ((uint32_t)upstreamResp[pos+5] << 16) |
                                                  ((uint32_t)upstreamResp[pos+6] << 8)  |
                                                               upstreamResp[pos+7];
                                uint16_t rdlen  = ((uint16_t)upstreamResp[pos+8] << 8) | upstreamResp[pos+9];
                                pos += 10;
                                if (rtype == DNS_TYPE_A && rclass == DNS_CLASS_IN &&
                                    rdlen == 4 && pos + 4 <= upstreamLen) {
                                    uint32_t rip = ((uint32_t)upstreamResp[pos]   << 24) |
                                                   ((uint32_t)upstreamResp[pos+1] << 16) |
                                                   ((uint32_t)upstreamResp[pos+2] << 8)  |
                                                               upstreamResp[pos+3];
                                    IPAddress resolved(rip >> 24, (rip >> 16) & 0xFF,
                                                       (rip >> 8) & 0xFF, rip & 0xFF);
                                    Serial.printf("[DNS] '%s' → %s (upstream, TTL=%lu)\n",
                                                  name, resolved.toString().c_str(),
                                                  (unsigned long)rttl);
                                    _cache.insert(name, rip, rttl);
                                }
                            }
                        }
                    }
                }
                return; // already sent upstream response
            } else {
                // Upstream failed — return SERVFAIL
                Serial.printf("[DNS] '%s' → SERVFAIL (upstream relay failed)\n", name);
                respLen = buildServfail(resp, sizeof(resp), txId, name);
            }
        }
    }

    if (respLen > 0) {
        sendto(_sock, resp, respLen, 0, client, clientLen);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper
// ─────────────────────────────────────────────────────────────────────────────

uint32_t dnsParseIp(const char* str) {
    if (!str) return 0;
    unsigned int a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    return (a << 24) | (b << 16) | (c << 8) | d;
}

#endif // !ESP8266
