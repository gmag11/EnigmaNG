// MqttNode Example - EnigmaNG
// Mesh node (ESP32) that joins the EnigmaNG mesh and publishes a test MQTT
// message every 10 seconds.  IP connectivity is provided by the mesh, so no
// direct WiFi credentials are needed here — the gateway handles uplink.
//
// Requires:
//   - A running EnigmaNG gateway with internet/LAN uplink
//   - An accessible MQTT broker (e.g. Mosquitto on the LAN, or test.mosquitto.org)
//   - PubSubClient library (Nick O'Leary) added to lib_deps in platformio.ini

#include <Arduino.h>
#include <MeshNetwork.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

MeshNetwork mesh;

// ─── CONFIGURATION ──────────────────────────────────────────────
// Pre-Shared Key (must be the same on all nodes in the network)
const char* PSK = "MySecretMeshKey123";

// Channel (must match gateway)
const uint8_t CHANNEL = 6;

// MQTT broker settings
const char*    MQTT_HOST  = "192.168.5.120";   // IP or hostname of your MQTT broker
const uint16_t MQTT_PORT  = 1883;
const char*    MQTT_USER  = "homiot";                // Leave empty if no auth
const char*    MQTT_PASS  = "DP%tL7CZWQ&W";                // Leave empty if no auth

// Topic to publish on
const char* MQTT_TOPIC = "enigma/test";

// Publish interval (ms)
const uint32_t PUBLISH_INTERVAL_MS = 10000;
// ────────────────────────────────────────────────────────────────

WiFiClient   tcpClient;
PubSubClient mqttClient(tcpClient);

// ─── Helpers ─────────────────────────────────────────────────────

// Build a unique client ID from the chip MAC to avoid broker collisions
static String mqttClientId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "enigma-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// Attempt (re)connection to the MQTT broker.  Returns true on success.
static bool mqttReconnect() {
    if (mqttClient.connected()) return true;
    if (!mesh.isConnected()) {
        Serial.println("[MQTT] Mesh not connected yet — skipping broker connect");
        return false;
    }

    mqttClient.setServer(MQTT_HOST, MQTT_PORT);

    const String clientId = mqttClientId();
    Serial.printf("[MQTT] Connecting to %s:%u as %s …\n",
                  MQTT_HOST, MQTT_PORT, clientId.c_str());

    bool ok;
    if (strlen(MQTT_USER) > 0) {
        ok = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    } else {
        ok = mqttClient.connect(clientId.c_str());
    }

    if (ok) {
        Serial.println("[MQTT] Connected!");
    } else {
        Serial.printf("[MQTT] Connection failed, rc=%d — will retry later\n",
                      mqttClient.state());
    }
    return ok;
}

// ─── Callbacks ───────────────────────────────────────────────────

void onNodeJoin(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[+] Node joined: %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  ip.toString().c_str());
}

void onNodeLeave(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[-] Node left: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ─── Arduino entry points ────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=== EnigmaNG MqttNode ===");
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

    Serial.printf("[MQTT] Will publish to topic \"%s\" every %lus\n",
                  MQTT_TOPIC, (unsigned long)(PUBLISH_INTERVAL_MS / 1000));
}

void loop() {
    mesh.loop();

    // Keep MQTT connection alive
    if (!mqttClient.connected()) {
        static uint32_t lastReconnect = 0;
        if (millis() - lastReconnect > 5000) {   // Retry every 5 s
            lastReconnect = millis();
            mqttReconnect();
        }
    }
    mqttClient.loop();

    // Publish a test message every PUBLISH_INTERVAL_MS
    static uint32_t lastPublish = 0;
    if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
        lastPublish = millis();

        if (mqttClient.connected()) {
            char payload[64];
            snprintf(payload, sizeof(payload),
                     "{\"uptime\":%lu,\"heap\":%lu,\"ip\":\"%s\"}",
                     (unsigned long)(millis() / 1000),
                     (unsigned long)ESP.getFreeHeap(),
                     mesh.getLocalIP().toString().c_str());

            bool published = mqttClient.publish(MQTT_TOPIC, payload);
            Serial.printf("[MQTT] Publish -> %s : %s [%s]\n",
                          MQTT_TOPIC, payload, published ? "OK" : "FAIL");
        } else {
            Serial.println("[MQTT] Not connected — skipping publish");
        }
    }
}
