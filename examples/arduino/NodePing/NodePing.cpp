// NodePing Example - EnigmaNG
// Mesh node (ESP32) that periodically pings a fixed host on the local
// network (e.g. the WiFi router / gateway) to verify IP transparency
// end-to-end: mesh0 → wifi_sta → LAN target.
//
// Requires a running EnigmaNG gateway with uplink and NAT enabled.

#include <Arduino.h>
#include <MeshNetwork.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

MeshNetwork mesh;

// ─── CONFIGURATION ──────────────────────────────────────────────
// Pre-Shared Key (must be the same on all nodes in the network)
const char* PSK       = "MySecretMeshKey123";

// Channel (must match gateway; 0 = auto-scan, not yet implemented)
const uint8_t CHANNEL = 6;

// Host to ping — accepts either a dotted-decimal IP ("8.8.8.8") or a hostname
// ("google.com") so you can exercise the mesh DNS proxy end-to-end.
const char* PING_TARGET = "google.com";

// Interval between ping sweeps (ms)
const uint32_t PING_INTERVAL_MS = 10000;
// ────────────────────────────────────────────────────────────────

// ─── Minimal ICMP helper ─────────────────────────────────────────

#pragma pack(push, 1)
struct IcmpEchoHdr {
    uint8_t  type;    // 8 = echo request, 0 = echo reply
    uint8_t  code;
    uint16_t chksum;
    uint16_t id;
    uint16_t seq;
};
#pragma pack(pop)

static uint16_t icmpChecksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (uint16_t)(data[i] << 8 | data[i + 1]);
    }
    if (len & 1) sum += (uint16_t)(data[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

// Sends one ICMP echo request to `target`, waits up to `timeoutMs` for reply.
// Returns true and fills *rttMs on success.
static bool pingOnce(IPAddress target, uint32_t* rttMs, uint32_t timeoutMs = 3000) {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        Serial.printf("[Ping] socket() failed: errno=%d\n", errno);
        return false;
    }

    struct timeval tv = { (long)(timeoutMs / 1000), (long)((timeoutMs % 1000) * 1000) };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static uint16_t seqCounter = 0;
    const uint16_t id  = (uint16_t)(xTaskGetTickCount() & 0xFFFF);
    const uint16_t seq = ++seqCounter;

    uint8_t pkt[40] = {};
    IcmpEchoHdr* hdr = (IcmpEchoHdr*)pkt;
    hdr->type   = 8;  // Echo request
    hdr->code   = 0;
    hdr->id     = htons(id);
    hdr->seq    = htons(seq);
    for (size_t i = sizeof(IcmpEchoHdr); i < sizeof(pkt); i++) pkt[i] = (uint8_t)i;
    hdr->chksum = htons(icmpChecksum(pkt, sizeof(pkt)));

    struct sockaddr_in dest = {};
    dest.sin_family      = AF_INET;
    dest.sin_addr.s_addr = (uint32_t)target;

    uint32_t t0 = millis();
    if (sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        Serial.printf("[Ping] sendto() failed: errno=%d\n", errno);
        close(sock);
        return false;
    }

    uint8_t rxBuf[128];
    bool success = false;
    while (true) {
        struct sockaddr_in from = {};
        socklen_t fromLen = sizeof(from);
        int n = recvfrom(sock, rxBuf, sizeof(rxBuf), 0, (struct sockaddr*)&from, &fromLen);
        if (n < 0) break;  // timeout

        int ipHdrLen = (rxBuf[0] & 0x0F) * 4;
        if (n < ipHdrLen + (int)sizeof(IcmpEchoHdr)) continue;

        IcmpEchoHdr* reply = (IcmpEchoHdr*)(rxBuf + ipHdrLen);
        if (reply->type == 0 &&
            ntohs(reply->id)  == id &&
            ntohs(reply->seq) == seq &&
            from.sin_addr.s_addr == (uint32_t)target) {
            if (rttMs) *rttMs = millis() - t0;
            success = true;
            break;
        }
    }

    close(sock);
    return success;
}

// ─── Raw UDP DNS query ────────────────────────────────────────────
// Sends a minimal A-record query directly to `dnsServer` via UDP/53.
// Bypasses lwIP's resolver stack — confirms whether UDP reaches the proxy.
static void rawDnsQuery(IPAddress dnsServer, const char* host) {
    Serial.printf("[DNS ] rawDnsQuery: %s -> %s:53\n", host, dnsServer.toString().c_str());

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        Serial.printf("[DNS ] rawDnsQuery: socket() failed errno=%d\n", errno);
        return;
    }

    // Build a minimal DNS A query
    uint8_t pkt[256] = {};
    pkt[0] = 0x12; pkt[1] = 0x34;  // Transaction ID
    pkt[2] = 0x01; pkt[3] = 0x00;  // Flags: standard query
    pkt[4] = 0x00; pkt[5] = 0x01;  // QDCOUNT = 1
    // Encode hostname into DNS wire format
    size_t pos = 12;
    const char* p = host;
    while (*p) {
        const char* dot = strchr(p, '.');
        size_t labLen = dot ? (size_t)(dot - p) : strlen(p);
        pkt[pos++] = (uint8_t)labLen;
        memcpy(&pkt[pos], p, labLen);
        pos += labLen;
        if (!dot) break;
        p = dot + 1;
    }
    pkt[pos++] = 0x00;        // root label
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  // QTYPE  = A
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  // QCLASS = IN

    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(53);
    dst.sin_addr.s_addr = (uint32_t)dnsServer;

    if (sendto(sock, pkt, pos, 0, (struct sockaddr*)&dst, sizeof(dst)) < 0) {
        Serial.printf("[DNS ] rawDnsQuery: sendto() failed errno=%d\n", errno);
        close(sock);
        return;
    }
    Serial.printf("[DNS ] rawDnsQuery: sent %d bytes, waiting for reply...\n", (int)pos);

    struct timeval tv = { 3, 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t resp[512];
    ssize_t rcvd = recv(sock, resp, sizeof(resp), 0);
    close(sock);

    if (rcvd < 0) {
        Serial.printf("[DNS ] rawDnsQuery: no reply (timeout/error errno=%d)\n", errno);
        return;
    }
    Serial.printf("[DNS ] rawDnsQuery: got %d byte reply\n", (int)rcvd);
    if (rcvd >= 7) {
        uint8_t rcode = resp[3] & 0x0F;
        uint16_t ancount = ((uint16_t)resp[6] << 8) | resp[7];
        Serial.printf("[DNS ] rawDnsQuery: RCODE=%d ANCOUNT=%d\n", rcode, ancount);
        if (rcode == 0 && ancount > 0 && rcvd >= 16) {
            // Find first A record answer (simple scan for RDATA of type A)
            for (ssize_t i = 12; i + 12 <= rcvd; i++) {
                if (resp[i] == 0xC0 && i + 12 <= rcvd) {  // compressed name pointer
                    uint16_t t = ((uint16_t)resp[i+2] << 8) | resp[i+3];
                    uint16_t l = ((uint16_t)resp[i+10] << 8) | resp[i+11];
                    if (t == 1 && l == 4 && i + 16 <= rcvd) {  // A record, 4 bytes
                        Serial.printf("[DNS ] rawDnsQuery: %s -> %d.%d.%d.%d\n",
                                      host, resp[i+12], resp[i+13], resp[i+14], resp[i+15]);
                        break;
                    }
                }
            }
        }
    }
}

// ─── Resolve a hostname or IP string to an IPAddress ────────────
// Returns true and fills *out on success; false if resolution failed.
static bool resolveHost(const char* host, IPAddress* out) {
    // Fast path: dotted-decimal IP
    if (out->fromString(host)) {
        return true;
    }

    // Slow path: DNS lookup via lwIP getaddrinfo
    // Log which DNS server lwIP is currently configured to use
    for (int i = 0; i < 2; i++) {
        const ip_addr_t* srv = dns_getserver(i);
        if (srv && !ip_addr_isany(srv)) {
            Serial.printf("[DNS ] Server[%d]: %s\n", i, ipaddr_ntoa(srv));
        }
    }

    // Raw UDP test: bypasses lwIP resolver — confirms UDP reaches the proxy
    const ip_addr_t* dns0 = dns_getserver(0);
    if (dns0 && !ip_addr_isany(dns0)) {
        IPAddress dnsIP(dns0->u_addr.ip4.addr);
        rawDnsQuery(dnsIP, host);
    } else {
        Serial.println("[DNS ] rawDnsQuery: skipped (no DNS server configured)");
    }

    struct addrinfo hints = {};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    struct addrinfo* res = nullptr;
    int err = getaddrinfo(host, nullptr, &hints, &res);
    if (err != 0 || res == nullptr) {
        // Decode common lwIP EAI_* error codes (defined in lwip/netdb.h starting at 200)
        const char* errStr = "unknown";
        switch (err) {
            case 200: errStr = "EAI_NONAME";     break;
            case 201: errStr = "EAI_SERVICE";    break;
            case 202: errStr = "EAI_FAIL";       break;
            case 203: errStr = "EAI_MEMORY";     break;
            case 204: errStr = "EAI_FAMILY";     break;
            case 207: errStr = "EAI_NODATA";     break;
            case 210: errStr = "EAI_AGAIN";      break;
        }
        Serial.printf("[DNS ] Could not resolve '%s': err=%d (%s)\n", host, err, errStr);
        return false;
    }
    auto* sa = (struct sockaddr_in*)res->ai_addr;
    *out = IPAddress(sa->sin_addr.s_addr);
    Serial.printf("[DNS ] %s -> %s\n", host, out->toString().c_str());
    freeaddrinfo(res);
    return true;
}

// ─── Ping a single host, print result ────────────────────────────

static void doPing(const char* label, IPAddress target) {
    uint32_t rtt = 0;
    bool ok = pingOnce(target, &rtt);
    if (ok) {
        Serial.printf("[Ping] %-18s  RTT=%4lu ms  OK\n", label, (unsigned long)rtt);
    } else {
        Serial.printf("[Ping] %-18s  TIMEOUT\n", label);
    }
}

// ─── Node join / leave callbacks ─────────────────────────────────

void onNodeJoin(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[+] Node joined: %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  ip.toString().c_str());
}

void onNodeLeave(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[-] Node left: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ─── Arduino setup / loop ────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=== EnigmaNG NodePing ===");
    Serial.printf("SDK: %s  |  Heap: %lu B\n",
                  ESP.getSdkVersion(), (unsigned long)ESP.getFreeHeap());

    mesh.setChannel(CHANNEL);
    mesh.onNodeJoin(onNodeJoin);
    mesh.onNodeLeave(onNodeLeave);

    if (!mesh.begin(PSK, MESH_NODE)) {
        Serial.println("[FAIL] Could not start mesh node — halting.");
        while (true) delay(1000);
    }

    Serial.println("[OK] Mesh node started");
    Serial.printf("     Local IP : %s\n", mesh.getLocalIP().toString().c_str());
    Serial.printf("     Channel  : %d\n", mesh.getChannel());
    Serial.printf("     Target   : %s  (every %lu s, DNS resolved each sweep)\n",
                  PING_TARGET, (unsigned long)(PING_INTERVAL_MS / 1000));
    Serial.println();
}

void loop() {
    mesh.loop();

    // Periodic ping sweep
    static uint32_t lastPing = 0;
    if (millis() - lastPing >= PING_INTERVAL_MS) {
        lastPing = millis();

        if (!mesh.isConnected()) {
            Serial.println("[Ping] Not connected to mesh yet — skipping");
            return;
        }

        // 1. Ping mesh gateway (verifies mesh0 → gateway link)
        IPAddress gwIP = mesh.getGatewayIP();
        if (gwIP != IPAddress(0, 0, 0, 0)) {
            doPing(gwIP.toString().c_str(), gwIP);
        } else {
            Serial.println("[Ping] Gateway IP unknown — skipping gateway ping");
        }

        // 2. Ping configured target (IP or hostname — verifies end-to-end: mesh0 → NAT → DNS → LAN)
        IPAddress target;
        if (!resolveHost(PING_TARGET, &target)) {
            Serial.printf("[Ping] Could not resolve target: %s\n", PING_TARGET);
            return;
        }
        doPing(PING_TARGET, target);
    }

    // Status heartbeat every 30 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus >= 30000) {
        lastStatus = millis();
        const ip_addr_t* dns0 = dns_getserver(0);
        Serial.printf("[Status] connected=%s  nodes=%d  ip=%s  dns=%s  heap=%lu  uptime=%lus\n",
                      mesh.isConnected() ? "YES" : "no",
                      mesh.getNodeCount(),
                      mesh.getLocalIP().toString().c_str(),
                      (dns0 && !ip_addr_isany(dns0)) ? ipaddr_ntoa(dns0) : "none",
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)(millis() / 1000));
    }
}
