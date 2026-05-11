// GatewaySingleChip Example - EnigmaNG
// ESP32 acting as mesh gateway with WiFi uplink (single-chip, STA+AP dual mode)
// The Web UI is accessible from the mesh network IP on port 80.

#include <Arduino.h>
#include <MeshNetwork.h>

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

    Serial.println("\n--- Gateway ready. Nodes can now join. ---\n");
}

void loop() {
    mesh.loop();

    // Print status every 15 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 15000) {
        lastStatus = millis();
        Serial.printf("[Gateway] Nodes: %d | Heap: %lu | Uptime: %lus\n",
                      mesh.getNodeCount(),
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)(millis() / 1000));
    }
}
