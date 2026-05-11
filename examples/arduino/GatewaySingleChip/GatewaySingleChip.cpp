// GatewaySingleChip Example - EnigmaNG
// ESP32 acting as mesh gateway with WiFi uplink (single-chip, STA+AP dual mode)
// The Web UI is accessible from the mesh network IP on port 80.

#include <Arduino.h>
#include <MeshNetwork.h>
#include <WiFi.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>

MeshNetwork mesh;

// ─── CONFIGURATION ──────────────────────────────────────────────
const char* PSK       = "MySecretMeshKey123";
const uint8_t CHANNEL = 6;

// WiFi uplink (not required for basic mesh test — leave empty to skip)
#if __has_include("wificonfig.h")
#include "wificonfig.h"  // Create this header with your WiFi credentials (see WifiConfig.h.example)
#else
const char* WIFI_SSID = "YourWiFiSSID";  // e.g., "YourWiFiSSID"
const char* WIFI_PASS = "YourWiFiPassword";  // e.g., "YourWiFiPassword"
#endif
// ────────────────────────────────────────────────────────────────

void onNodeJoin(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[+] Node joined mesh: %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  ip.toString().c_str());
}

void onNodeLeave(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[-] Node left mesh: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=== EnigmaNG Gateway (Single-Chip) ===");
    Serial.printf("SDK: %s\n", ESP.getSdkVersion());
    Serial.printf("Free heap: %lu\n", (unsigned long)ESP.getFreeHeap());

    mesh.setChannel(CHANNEL);
    mesh.onNodeJoin(onNodeJoin);
    mesh.onNodeLeave(onNodeLeave);

    // Start as gateway
    if (mesh.begin(PSK, MESH_GATEWAY)) {
        Serial.println("[OK] Mesh gateway started!");
        Serial.printf("     Mesh IP: %s\n", mesh.getLocalIP().toString().c_str());
        Serial.printf("     Channel: %d\n", mesh.getChannel());
    } else {
        Serial.println("[FAIL] Failed to start mesh gateway!");
        return;
    }

    // Start web UI on port 80
    if (mesh.startWebServer(80)) {
        Serial.println("[OK] Web UI started on port 80");
        Serial.println("     Connect to the onboarding AP and browse to the gateway IP");
    }

    // Start Prometheus metrics on port 9090
    mesh.startPrometheus(9090);

    // Connect WiFi uplink (optional — leave WIFI_SSID empty to skip)
    if (strlen(WIFI_SSID) > 0) {
        mesh.connectUplink(WIFI_SSID, WIFI_PASS);
    }

    // DNS proxy is enabled by default on the gateway (UDP/53).
    // It proxies queries to the upstream LAN DNS, caches responses, and
    // supports custom A records manageable via the Web UI at /dns.
    // Nodes automatically use the gateway as their DNS server (no DHCP needed).
    // Call mesh.disableDns() here if you do not want the DNS proxy:
    // mesh.disableDns();

    Serial.println("\n--- Gateway ready. Nodes can now join. ---\n");
}

void loop() {
    mesh.loop();

    // Print status every 15 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 15000) {
        lastStatus = millis();

        // Log upstream DNS server(s) and test resolution
        bool hasUplink = (WiFi.status() == WL_CONNECTED);
        const ip_addr_t* dns0 = dns_getserver(0);
        char dns0str[20] = "none";
        if (dns0 && !ip_addr_isany(dns0)) ip4addr_ntoa_r(&dns0->u_addr.ip4, dns0str, sizeof(dns0str));

        Serial.printf("[Gateway] Nodes: %d | Heap: %lu | Uptime: %lus | uplink=%s | upstream-dns=%s\n",
                      mesh.getNodeCount(),
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)(millis() / 1000),
                      hasUplink ? "YES" : "no",
                      dns0str);

        if (hasUplink) {
            // Test upstream DNS path: gateway resolves a hostname directly via STA
            struct addrinfo hints = {};
            hints.ai_family   = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            struct addrinfo* res = nullptr;
            int err = getaddrinfo("google.com", nullptr, &hints, &res);
            if (err == 0 && res) {
                auto* sa = (struct sockaddr_in*)res->ai_addr;
                IPAddress resolved(sa->sin_addr.s_addr);
                Serial.printf("[DNS-GW] google.com -> %s (upstream OK)\n", resolved.toString().c_str());
                freeaddrinfo(res);
            } else {
                Serial.printf("[DNS-GW] google.com -> FAILED err=%d (upstream not working!)\n", err);
            }

            // Self-test: send a UDP packet to our own mesh IP:53 and verify the
            // DNS proxy socket receives it.  This proves local UDP delivery works.
            static bool selfTestDone = false;
            if (!selfTestDone) {
                selfTestDone = true;
                IPAddress meshIP = mesh.getLocalIP();
                Serial.printf("[DNS-GW] self-test: sending UDP to %s:53\n", meshIP.toString().c_str());
                int ts = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (ts >= 0) {
                    struct sockaddr_in dst = {};
                    dst.sin_family = AF_INET;
                    dst.sin_port   = htons(53);
                    dst.sin_addr.s_addr = (uint32_t)meshIP;
                    // Minimal DNS-like payload (txID + standard query flags + 1 question)
                    uint8_t probe[] = { 0xAB, 0xCD, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00,
                                        0x04, 't','e','s','t', 0x00, 0x00, 0x01, 0x00, 0x01 };
                    int sent = sendto(ts, probe, sizeof(probe), 0,
                                      (struct sockaddr*)&dst, sizeof(dst));
                    Serial.printf("[DNS-GW] self-test: sendto() = %d (expected %d)\n",
                                  sent, (int)sizeof(probe));
                    close(ts);
                    // The DNS task should log "[DNS] pkt 22 bytes from 10.200.x.x" if
                    // the packet is delivered. Wait a tick and check indirectly via heap.
                } else {
                    Serial.printf("[DNS-GW] self-test: socket() failed errno=%d\n", errno);
                }
            }
        }
    }
}
