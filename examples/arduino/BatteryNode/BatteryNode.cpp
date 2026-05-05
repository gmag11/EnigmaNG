// BatteryNode Example - EnigmaNG
// Low-power mesh node with deep sleep cycle (LoRaWAN Class A style)

#include <Arduino.h>
#include <MeshNetwork.h>

MeshNetwork mesh;

const char* PSK = "MySecretMeshKey123";
const uint32_t SLEEP_INTERVAL_SEC = 60;  // Wake every 60 seconds

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== EnigmaNG BatteryNode ===");

    // Configure battery mode
    mesh.setBatteryMode(true, SLEEP_INTERVAL_SEC);

    if (mesh.begin(PSK, MESH_BATTERY)) {
        Serial.println("Battery node started!");
        Serial.printf("Local IP: %s\n", mesh.getLocalIP().toString().c_str());
        Serial.printf("Mesh time: %ld\n", (long)mesh.getMeshTime());
    } else {
        Serial.println("ERROR: Failed to start mesh!");
    }
}

void loop() {
    // In MESH_BATTERY mode, loop() handles:
    // 1. TX UPLINK to parent
    // 2. RX1 window (2s)
    // 3. RX2 window (2s)
    // 4. Enter deep sleep
    // The node will automatically enter deep sleep after the RX windows
    mesh.loop();
}
