// MeshNode8266 Example — EnigmaNG
// ESP8266 node that publishes sensor data over the mesh via an ESP32 proxy.
// Uses the MQTT-over-mesh protocol (PROXY_*) — no direct IP stack needed.

#include <Arduino.h>
#include <MeshNode8266.h>

MeshNode8266 mesh;

// ─── CONFIGURATION ──────────────────────────────────────────────
const char* PSK       = "MySecretMeshKey123";
const uint8_t CHANNEL = 6;   // 0 = auto-scan
// ────────────────────────────────────────────────────────────────

static uint32_t lastPublish = 0;

void onMqttMessage(const char* topic, const uint8_t* payload, size_t len) {
    Serial.printf("[MQTT] Received: %s = ", topic);
    Serial.write(payload, len);
    Serial.println();
}

void onConnected() {
    Serial.println("[Mesh] Connected to proxy — subscribing...");
    mesh.mqttSubscribe("enigma/cmd/#");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== EnigmaNG MeshNode8266 ===");

    mesh.onMqttMessage(onMqttMessage);
    mesh.onConnected(onConnected);

    if (!mesh.begin(PSK, CHANNEL)) {
        Serial.println("[FAIL] Could not initialize mesh node — halting.");
        while (true) delay(1000);
    }

    Serial.println("[OK] Mesh node started — scanning for proxy...");
}

void loop() {
    mesh.loop();

    // Publish sensor data every 10 seconds once connected
    if (mesh.isConnected() && millis() - lastPublish >= 10000) {
        lastPublish = millis();

        // Example: read ADC and publish
        int sensorValue = analogRead(A0);
        char payload[16];
        int len = snprintf(payload, sizeof(payload), "%d", sensorValue);

        if (mesh.mqttPublish("enigma/sensor/adc", (const uint8_t*)payload, len)) {
            Serial.printf("[MQTT] Published: adc=%d\n", sensorValue);
        } else {
            Serial.println("[MQTT] Publish failed");
        }
    }
}
