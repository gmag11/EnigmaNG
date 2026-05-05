// BasicNode Example - EnigmaNG
// Simple mesh node that joins the network and can communicate via standard TCP/IP

#include <Arduino.h>
#include <MeshNetwork.h>

MeshNetwork mesh;

// ─── CONFIGURATION ──────────────────────────────────────────────
// Pre-Shared Key (must be the same on all nodes in the network)
const char* PSK = "MySecretMeshKey123";

// Channel (must match gateway). Set 0 for auto-scan (not yet implemented)
const uint8_t CHANNEL = 6;
// ────────────────────────────────────────────────────────────────

void onNodeJoin(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[+] Node joined: %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  ip.toString().c_str());
}

void onNodeLeave(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[-] Node left: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=== EnigmaNG BasicNode ===");
    Serial.printf("SDK: %s\n", ESP.getSdkVersion());
    Serial.printf("Free heap: %lu\n", (unsigned long)ESP.getFreeHeap());

    mesh.setChannel(CHANNEL);
    mesh.onNodeJoin(onNodeJoin);
    mesh.onNodeLeave(onNodeLeave);

    if (mesh.begin(PSK, MESH_NODE)) {
        Serial.println("[OK] Mesh started successfully!");
        Serial.printf("     Local IP: %s\n", mesh.getLocalIP().toString().c_str());
        Serial.printf("     Channel: %d\n", mesh.getChannel());
    } else {
        Serial.println("[FAIL] Failed to start mesh!");
    }
}

void loop() {
    mesh.loop();

    // Print status every 10 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 10000) {
        lastStatus = millis();
        Serial.printf("[Status] Connected: %s | Nodes: %d | IP: %s | Heap: %lu\n",
                      mesh.isConnected() ? "YES" : "no",
                      mesh.getNodeCount(),
                      mesh.getLocalIP().toString().c_str(),
                      (unsigned long)ESP.getFreeHeap());
    }
}
