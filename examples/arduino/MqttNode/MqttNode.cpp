// MqttNode Example - EnigmaNG
// Mesh node (ESP32) that joins the EnigmaNG mesh and publishes a test MQTT
// message every 10 seconds using the ESP-IDF MQTT client (esp_mqtt).
// IP connectivity is provided by the mesh, so no direct WiFi credentials
// are needed here — the gateway handles uplink.
//
// Requires:
//   - A running EnigmaNG gateway with internet/LAN uplink
//   - An accessible MQTT broker (e.g. Mosquitto on the LAN)

#include <Arduino.h>
#include <MeshNetwork.h>
#include <WiFi.h>
#include "mqtt_client.h"

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

static esp_mqtt_client_handle_t mqttClient      = nullptr;
static volatile bool            mqttConnected   = false;
static volatile bool            mqttStartPending = false;  // set from mesh task, consumed in loop()

// Static storage — kept off the stack to avoid overflow in esp_mqtt_client_init()
static char                     s_uri[64];
static char                     s_clientId[24];
static esp_mqtt_client_config_t s_cfg;

// ─── Helpers ─────────────────────────────────────────────────────

// Build a unique client ID from the chip MAC to avoid broker collisions
static void mqttBuildClientId(char* buf, size_t len) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(buf, len, "enigma-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void mqttEventHandler(void* /*handler_args*/, esp_event_base_t /*base*/,
                             int32_t event_id, void* /*event_data*/) {
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            mqttConnected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqttConnected = false;
            break;
        default:
            break;
    }
}

// Forward declaration
static void mqttInitTask(void* arg);

// Called from loop() — spawns a background task so loop() is never blocked
static void mqttStart() {
    if (mqttClient) return;
    xTaskCreate(mqttInitTask, "mqtt_init", 6144, nullptr, 1, nullptr);
}

// All MQTT init + start runs in a background task to avoid blocking loop()
static void mqttInitTask(void* /*arg*/) {
    mqttBuildClientId(s_clientId, sizeof(s_clientId));
    snprintf(s_uri, sizeof(s_uri), "mqtt://%s:%u", MQTT_HOST, MQTT_PORT);

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.broker.address.uri    = s_uri;
    s_cfg.credentials.client_id = s_clientId;
    s_cfg.task.priority         = 1;  // keep same priority as loop()
    if (strlen(MQTT_USER) > 0) {
        s_cfg.credentials.username                = MQTT_USER;
        s_cfg.credentials.authentication.password = MQTT_PASS;
    }

    mqttClient = esp_mqtt_client_init(&s_cfg);
    if (!mqttClient) {
        Serial.println("[MQTT] init failed");
        vTaskDelete(nullptr);
        return;
    }

    esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler, nullptr);

    esp_err_t err = esp_mqtt_client_start(mqttClient);
    if (err != ESP_OK) {
        Serial.printf("[MQTT] start failed: 0x%x\n", err);
    } else {
        Serial.printf("[MQTT] Client started -> %s as %s\n", s_uri, s_clientId);
    }
    vTaskDelete(nullptr);
}

// ─── Callbacks ───────────────────────────────────────────────────

void onNodeJoin(const uint8_t* mac, IPAddress ip) {
    Serial.printf("[+] Node joined: %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  ip.toString().c_str());
    mqttStartPending = true;
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

    if (mqttStartPending) {
        mqttStartPending = false;
        mqttStart();
    }

    // Report MQTT connection state changes
    static bool lastConnState = false;
    if (mqttConnected != lastConnState) {
        lastConnState = mqttConnected;
        Serial.printf("[MQTT] %s\n", mqttConnected ? "Connected" : "Disconnected");
    }

    // Publish a test message every PUBLISH_INTERVAL_MS
    static uint32_t lastPublish = 0;
    if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
        lastPublish = millis();

        if (mqttConnected) {
            char payload[64];
            snprintf(payload, sizeof(payload),
                     "{\"uptime\":%lu,\"heap\":%lu,\"ip\":\"%s\"}",
                     (unsigned long)(millis() / 1000),
                     (unsigned long)ESP.getFreeHeap(),
                     mesh.getLocalIP().toString().c_str());

            int msgId = esp_mqtt_client_publish(mqttClient, MQTT_TOPIC,
                                                payload, 0, /*qos=*/1, /*retain=*/0);
            Serial.printf("[MQTT] Publish -> %s : %s [msgId=%d]\n",
                          MQTT_TOPIC, payload, msgId);
        } else {
            Serial.println("[MQTT] Not connected — skipping publish");
        }
    }
}
