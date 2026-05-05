// BasicNode Example - EnigmaNG
// Simple mesh node that joins the network and can communicate via standard TCP/IP

#include <Arduino.h>
#include <MeshNetwork.h>

MeshNetwork mesh;

// Pre-Shared Key (must be the same on all nodes in the network)
const char* PSK = "MySecretMeshKey123";

void onNodeJoin(const uint8_t* mac, IPAddress ip) {
    Serial.printf("Node joined: %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  ip.toString().c_str());
}

void onNodeLeave(const uint8_t* mac, IPAddress ip) {
    Serial.printf("Node left: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== EnigmaNG BasicNode ===");

    mesh.onNodeJoin(onNodeJoin);
    mesh.onNodeLeave(onNodeLeave);

    if (mesh.begin(PSK, MESH_NODE)) {
        Serial.println("Mesh started successfully!");
        Serial.printf("Local IP: %s\n", mesh.getLocalIP().toString().c_str());
    } else {
        Serial.println("ERROR: Failed to start mesh!");
    }
}

void loop() {
    mesh.loop();

    // Print status every 30 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 30000) {
        lastStatus = millis();
        Serial.printf("[Status] Connected: %s, Nodes: %d, IP: %s\n",
                      mesh.isConnected() ? "yes" : "no",
                      mesh.getNodeCount(),
                      mesh.getLocalIP().toString().c_str());
    }
}
