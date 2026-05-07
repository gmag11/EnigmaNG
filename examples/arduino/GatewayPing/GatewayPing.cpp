// GatewayPing Example - EnigmaNG
// Gateway that sends an ICMP ping every 10 seconds to every node in the
// routing table and logs the RTT. Useful for verifying IP transparency
// over the mesh (task #37: transparent ping over mesh0).

#include <Arduino.h>
#include <MeshNetwork.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

MeshNetwork mesh;

// ─── CONFIGURATION ──────────────────────────────────────────────
const char* PSK       = "MySecretMeshKey123";
const uint8_t CHANNEL = 6;

const char* WIFI_SSID = "";   // Leave empty to run mesh-only (no uplink)
const char* WIFI_PASS = "";
// ────────────────────────────────────────────────────────────────

// ─── Minimal ICMP header ─────────────────────────────────────────

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

// Sends one ICMP echo request to target, waits up to timeoutMs for reply.
// Returns true and sets *rttMs on success.
static bool pingOnce(IPAddress target, uint32_t* rttMs, uint32_t timeoutMs = 3000) {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        Serial.printf("[Ping] socket() failed: errno=%d\n", errno);
        return false;
    }

    // Set receive timeout
    struct timeval tv = { (long)(timeoutMs / 1000), (long)((timeoutMs % 1000) * 1000) };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Build ICMP echo request (8-byte header + 32-byte payload)
    static uint16_t seqCounter = 0;
    const uint16_t id  = (uint16_t)(xTaskGetTickCount() & 0xFFFF);
    const uint16_t seq = ++seqCounter;

    uint8_t pkt[40] = {};
    IcmpEchoHdr* hdr = (IcmpEchoHdr*)pkt;
    hdr->type   = 8;  // Echo request
    hdr->code   = 0;
    hdr->id     = htons(id);
    hdr->seq    = htons(seq);
    // Fill payload with pattern
    for (size_t i = sizeof(IcmpEchoHdr); i < sizeof(pkt); i++) pkt[i] = (uint8_t)i;
    hdr->chksum = htons(icmpChecksum(pkt, sizeof(pkt)));

    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = (uint32_t)target;

    uint32_t t0 = millis();
    if (sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        close(sock);
        return false;
    }

    // Receive loop — filter for our echo reply
    uint8_t rxBuf[128];
    bool success = false;
    while (true) {
        struct sockaddr_in from = {};
        socklen_t fromLen = sizeof(from);
        int n = recvfrom(sock, rxBuf, sizeof(rxBuf), 0, (struct sockaddr*)&from, &fromLen);
        if (n < 0) break;  // timeout

        // Skip IP header (IHL field, lower 4 bits of first byte × 4)
        int ipHdrLen = (rxBuf[0] & 0x0F) * 4;
        if (n < ipHdrLen + (int)sizeof(IcmpEchoHdr)) continue;

        IcmpEchoHdr* reply = (IcmpEchoHdr*)(rxBuf + ipHdrLen);
        if (reply->type == 0 &&                      // echo reply
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

// ─── Ping all nodes in routing table ─────────────────────────────

static void pingAllNodes() {
    Router& router = mesh.getRouter();
    size_t  count  = router.getRouteCount();

    if (count == 0) {
        Serial.println("[Ping] No routes — nothing to ping");
        return;
    }

    Serial.printf("[Ping] Pinging %u route(s)...\n", (unsigned)count);

    for (size_t i = 0; i < count; i++) {
        RouteEntry* r = router.getRouteByIndex(i);
        if (!r || !r->valid) continue;

        // Skip broadcast / unspecified / self
        if (r->destIP == IPAddress(0, 0, 0, 0)         ||
            r->destIP == IPAddress(255, 255, 255, 255)  ||
            r->destIP == mesh.getLocalIP()) continue;

        uint32_t rtt = 0;
        bool ok = pingOnce(r->destIP, &rtt);

        if (ok) {
            Serial.printf("[Ping] %-15s  hops=%u  RTT=%4lu ms  OK\n",
                          r->destIP.toString().c_str(),
                          (unsigned)r->hopCount,
                          (unsigned long)rtt);
        } else {
            Serial.printf("[Ping] %-15s  hops=%u  TIMEOUT\n",
                          r->destIP.toString().c_str(),
                          (unsigned)r->hopCount);
        }
    }
}

// ─── Node join / leave callbacks ─────────────────────────────────

void onNodeJoin(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[+] Node joined: %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  ip.toString().c_str());
    // Ping will happen in the next periodic sweep (loop), not here —
    // this callback runs inside the ESP-NOW receive task which has a small
    // stack and cannot handle blocking socket I/O.
}

void onNodeLeave(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[-] Node left: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ─── Arduino setup / loop ────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=== EnigmaNG GatewayPing ===");
    Serial.printf("SDK: %s  |  Heap: %lu B\n",
                  ESP.getSdkVersion(), (unsigned long)ESP.getFreeHeap());

    mesh.setChannel(CHANNEL);
    mesh.onNodeJoin(onNodeJoin);
    mesh.onNodeLeave(onNodeLeave);

    if (!mesh.begin(PSK, MESH_GATEWAY)) {
        Serial.println("[FAIL] Could not start mesh gateway — halting.");
        while (true) delay(1000);
    }

    Serial.println("[OK] Gateway started");
    Serial.printf("     Mesh IP : %s\n", mesh.getLocalIP().toString().c_str());
    Serial.printf("     Channel : %d\n", mesh.getChannel());

    mesh.startWebServer(80);
    mesh.startPrometheus(9090);
    Serial.println("[OK] Web UI on :80  |  Prometheus on :9090");

    if (strlen(WIFI_SSID) > 0) {
        mesh.connectUplink(WIFI_SSID, WIFI_PASS);
    }

    Serial.println("\n--- Gateway ready. Pinging nodes every 10 s ---\n");
}

void loop() {
    mesh.loop();

    // Periodic ping sweep every 10 seconds
    static uint32_t lastPing = 0;
    if (millis() - lastPing >= 10000) {
        lastPing = millis();
        pingAllNodes();
    }

    // Status heartbeat every 30 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus >= 30000) {
        lastStatus = millis();
        Serial.printf("[GW] nodes=%d  routes=%u  heap=%lu  uptime=%lus\n",
                      mesh.getNodeCount(),
                      (unsigned)mesh.getRouter().getRouteCount(),
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)(millis() / 1000));
    }
}

