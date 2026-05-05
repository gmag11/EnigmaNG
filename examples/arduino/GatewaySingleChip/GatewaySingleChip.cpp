// GatewaySingleChip Example - EnigmaNG
// ESP32 acting as mesh gateway with WiFi uplink (single-chip, STA+AP dual mode)

#include <Arduino.h>
#include <MeshNetwork.h>

MeshNetwork mesh;

const char* PSK = "MySecretMeshKey123";
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASS = "YourWiFiPassword";

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== EnigmaNG Gateway (Single-Chip) ===");

    // Start as gateway
    if (mesh.begin(PSK, MESH_GATEWAY)) {
        Serial.println("Mesh gateway started!");
        Serial.printf("Mesh IP: %s\n", mesh.getLocalIP().toString().c_str());
    } else {
        Serial.println("ERROR: Failed to start mesh gateway!");
        return;
    }

    // Start web UI on port 80
    if (mesh.startWebServer(80)) {
        Serial.println("Web UI available at http://" + mesh.getLocalIP().toString());
    }

    // Start Prometheus metrics
    mesh.startPrometheus(9090);
}

void loop() {
    mesh.loop();

    // Print status every 60 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 60000) {
        lastStatus = millis();
        Serial.printf("[Gateway] Nodes: %d, Uplink: %s\n",
                      mesh.getNodeCount(),
                      mesh.isConnected() ? "connected" : "disconnected");
    }
}
