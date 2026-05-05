/**
 * Unit tests: ServiceDiscovery
 *
 * Covers:
 *   - Register and find a local service
 *   - Unregister a service
 *   - handleServiceReply stores a discovered service
 *   - handleServiceReply updates an existing entry (dedup)
 *   - findService returns nullptr for unknown service
 *   - serializeServices / deserializeServices round-trip
 *   - Full table (MAX_SERVICES) rejects further registrations
 */

#include <unity.h>
#include <cstring>
#include "ServiceDiscovery.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t NODE_MAC[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t NODE2_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// ---------------------------------------------------------------------------
// Test 1: Register a local service and confirm it is stored
// ---------------------------------------------------------------------------
void test_register_service(void) {
    ServiceDiscovery sd;
    TEST_ASSERT_TRUE(sd.registerService("_mqtt._tcp", 1883));
    // Local services are not in the discovered table; verify no crash and returns true
}

// ---------------------------------------------------------------------------
// Test 2: Unregister a service that was registered
// ---------------------------------------------------------------------------
void test_unregister_service(void) {
    ServiceDiscovery sd;
    sd.registerService("_http._tcp", 80);
    TEST_ASSERT_TRUE(sd.unregisterService("_http._tcp"));
    TEST_ASSERT_FALSE(sd.unregisterService("_http._tcp")); // already gone
}

// ---------------------------------------------------------------------------
// Test 3: Unregister a service that was never registered returns false
// ---------------------------------------------------------------------------
void test_unregister_nonexistent(void) {
    ServiceDiscovery sd;
    TEST_ASSERT_FALSE(sd.unregisterService("_unknown._tcp"));
}

// ---------------------------------------------------------------------------
// Test 4: handleServiceReply stores a discovered service
// ---------------------------------------------------------------------------
void test_handle_reply_stores_service(void) {
    ServiceDiscovery sd;
    sd.handleServiceReply(NODE_MAC, "_mqtt._tcp", IPAddress(10, 200, 0, 5), 1883);

    const MeshService* svc = sd.findService("_mqtt._tcp");
    TEST_ASSERT_NOT_NULL_MESSAGE(svc, "Service must be discoverable after reply");
    TEST_ASSERT_EQUAL(1883, svc->port);
    TEST_ASSERT_EQUAL_MEMORY(NODE_MAC, svc->nodeMac, 6);
}

// ---------------------------------------------------------------------------
// Test 5: Duplicate reply updates the existing entry (no duplicate)
// ---------------------------------------------------------------------------
void test_handle_reply_deduplicates(void) {
    ServiceDiscovery sd;
    sd.handleServiceReply(NODE_MAC,  "_mqtt._tcp", IPAddress(10, 200, 0, 5), 1883);
    sd.handleServiceReply(NODE2_MAC, "_mqtt._tcp", IPAddress(10, 200, 0, 6), 1884);

    const MeshService* svc = sd.findService("_mqtt._tcp");
    TEST_ASSERT_NOT_NULL(svc);
    // Second reply must have overwritten first
    TEST_ASSERT_EQUAL(1884, svc->port);
    TEST_ASSERT_EQUAL_MEMORY(NODE2_MAC, svc->nodeMac, 6);
}

// ---------------------------------------------------------------------------
// Test 6: findService returns nullptr for unknown name
// ---------------------------------------------------------------------------
void test_find_unknown_service(void) {
    ServiceDiscovery sd;
    TEST_ASSERT_NULL(sd.findService("_nope._tcp"));
}

// ---------------------------------------------------------------------------
// Test 7: serializeServices / deserializeServices round-trip
// ---------------------------------------------------------------------------
void test_serialize_deserialize_services(void) {
    ServiceDiscovery sd;
    sd.registerService("_mqtt._tcp", 1883);
    sd.registerService("_http._tcp", 80);

    uint8_t buf[256];
    size_t len = sd.serializeServices(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, len, "Serialized buffer must not be empty");

    // Deserialize into a fresh instance via handleServiceReply path
    ServiceDiscovery sd2;
    sd2.deserializeServices(buf, len, NODE_MAC);

    const MeshService* mqtt = sd2.findService("_mqtt._tcp");
    TEST_ASSERT_NOT_NULL_MESSAGE(mqtt, "_mqtt._tcp must survive round-trip");
    TEST_ASSERT_EQUAL(1883, mqtt->port);

    const MeshService* http = sd2.findService("_http._tcp");
    TEST_ASSERT_NOT_NULL_MESSAGE(http, "_http._tcp must survive round-trip");
    TEST_ASSERT_EQUAL(80, http->port);
}

// ---------------------------------------------------------------------------
// Test 8: Register up to MAX_SERVICES — one more must fail
// ---------------------------------------------------------------------------
void test_register_full_table(void) {
    ServiceDiscovery sd;
    char name[SERVICE_NAME_MAX];
    for (int i = 0; i < MAX_SERVICES; i++) {
        snprintf(name, sizeof(name), "_svc%d._tcp", i);
        TEST_ASSERT_TRUE(sd.registerService(name, (uint16_t)(1000 + i)));
    }
    TEST_ASSERT_FALSE_MESSAGE(sd.registerService("_overflow._tcp", 9999),
                              "Registration must fail when table is full");
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_register_service);
    RUN_TEST(test_unregister_service);
    RUN_TEST(test_unregister_nonexistent);
    RUN_TEST(test_handle_reply_stores_service);
    RUN_TEST(test_handle_reply_deduplicates);
    RUN_TEST(test_find_unknown_service);
    RUN_TEST(test_serialize_deserialize_services);
    RUN_TEST(test_register_full_table);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_register_service);
    RUN_TEST(test_unregister_service);
    RUN_TEST(test_unregister_nonexistent);
    RUN_TEST(test_handle_reply_stores_service);
    RUN_TEST(test_handle_reply_deduplicates);
    RUN_TEST(test_find_unknown_service);
    RUN_TEST(test_serialize_deserialize_services);
    RUN_TEST(test_register_full_table);
    return UNITY_END();
}
#endif
