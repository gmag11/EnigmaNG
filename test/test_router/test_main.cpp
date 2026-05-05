#include <unity.h>
#include "Router.h"

void setUp(void) {}
void tearDown(void) {}

void test_add_and_find_route(void) {
    Router router;
    uint8_t destMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t nextHop[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    RouteEntry* entry = router.addRoute(IPAddress(10, 200, 0, 5), destMac, nextHop, 2);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL(2, entry->hopCount);

    RouteEntry* found = router.findRouteByIP(IPAddress(10, 200, 0, 5));
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_MEMORY(destMac, found->destMac, 6);
}

void test_route_update_better_hop(void) {
    Router router;
    uint8_t destMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    uint8_t nextHop1[6] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
    uint8_t nextHop2[6] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22};

    router.addRoute(IPAddress(10, 200, 0, 10), destMac, nextHop1, 5);
    router.addRoute(IPAddress(10, 200, 0, 10), destMac, nextHop2, 2);

    RouteEntry* found = router.findRouteByIP(IPAddress(10, 200, 0, 10));
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(2, found->hopCount);
    TEST_ASSERT_EQUAL_MEMORY(nextHop2, found->nextHopMac, 6);
}

void test_seen_frame_cache(void) {
    Router router;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    TEST_ASSERT_FALSE(router.isFrameSeen(mac, 100));
    router.markFrameSeen(mac, 100);
    TEST_ASSERT_TRUE(router.isFrameSeen(mac, 100));
    TEST_ASSERT_FALSE(router.isFrameSeen(mac, 101));
}

void test_route_remove(void) {
    Router router;
    uint8_t destMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
    uint8_t nextHop[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    router.addRoute(IPAddress(10, 200, 0, 20), destMac, nextHop, 1);
    TEST_ASSERT_EQUAL(1, router.getRouteCount());

    bool removed = router.removeRoute(IPAddress(10, 200, 0, 20));
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQUAL(0, router.getRouteCount());
    TEST_ASSERT_NULL(router.findRouteByIP(IPAddress(10, 200, 0, 20)));
}

void test_route_withdraw(void) {
    Router router;
    uint8_t destMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x04};
    uint8_t nextHop[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    router.addRoute(IPAddress(10, 200, 0, 30), destMac, nextHop, 3);
    TEST_ASSERT_EQUAL(1, router.getRouteCount());

    router.handleRouteWithdraw(destMac);
    TEST_ASSERT_EQUAL(0, router.getRouteCount());
}

void test_topology_changed_flag(void) {
    Router router;
    uint8_t destMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x05};
    uint8_t nextHop[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    TEST_ASSERT_FALSE(router.hasTopologyChanged());
    router.addRoute(IPAddress(10, 200, 0, 40), destMac, nextHop, 1);
    TEST_ASSERT_TRUE(router.hasTopologyChanged());
    router.clearTopologyChanged();
    TEST_ASSERT_FALSE(router.hasTopologyChanged());
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_add_and_find_route);
    RUN_TEST(test_route_update_better_hop);
    RUN_TEST(test_seen_frame_cache);
    RUN_TEST(test_route_remove);
    RUN_TEST(test_route_withdraw);
    RUN_TEST(test_topology_changed_flag);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_add_and_find_route);
    RUN_TEST(test_route_update_better_hop);
    RUN_TEST(test_seen_frame_cache);
    RUN_TEST(test_route_remove);
    RUN_TEST(test_route_withdraw);
    RUN_TEST(test_topology_changed_flag);
    return UNITY_END();
}
#endif
