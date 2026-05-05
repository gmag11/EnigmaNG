#include <unity.h>
#include "PeerManager.h"

void setUp(void) {}
void tearDown(void) {}

void test_add_and_find_peer(void) {
    PeerManager pm;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

    PeerEntry* peer = pm.addPeer(mac);
    TEST_ASSERT_NOT_NULL(peer);
    TEST_ASSERT_EQUAL_MEMORY(mac, peer->mac, 6);
    TEST_ASSERT_TRUE(peer->valid);

    PeerEntry* found = pm.findPeer(mac);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_MEMORY(mac, found->mac, 6);
}

void test_peer_not_found(void) {
    PeerManager pm;
    uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_NULL(pm.findPeer(mac));
}

void test_anti_replay(void) {
    PeerManager pm;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    pm.addPeer(mac);

    // First seq should pass
    TEST_ASSERT_TRUE(pm.checkAndUpdateSeqRx(mac, 1));
    // Same seq should fail (replay)
    TEST_ASSERT_FALSE(pm.checkAndUpdateSeqRx(mac, 1));
    // Higher seq should pass
    TEST_ASSERT_TRUE(pm.checkAndUpdateSeqRx(mac, 2));
    // Lower seq should fail
    TEST_ASSERT_FALSE(pm.checkAndUpdateSeqRx(mac, 1));
}

void test_sequence_tx(void) {
    PeerManager pm;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
    pm.addPeer(mac);

    TEST_ASSERT_EQUAL(1, pm.getNextSeqTx(mac));
    TEST_ASSERT_EQUAL(2, pm.getNextSeqTx(mac));
    TEST_ASSERT_EQUAL(3, pm.getNextSeqTx(mac));
}

void test_set_link_key(void) {
    PeerManager pm;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x04};
    uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    pm.addPeer(mac);
    bool ok = pm.setLinkKey(mac, key, 5);
    TEST_ASSERT_TRUE(ok);

    PeerEntry* peer = pm.findPeer(mac);
    TEST_ASSERT_NOT_NULL(peer);
    TEST_ASSERT_TRUE(peer->keyEstablished);
    TEST_ASSERT_EQUAL(5, peer->epoch);
    TEST_ASSERT_EQUAL_MEMORY(key, peer->linkKey, 16);
}

void test_eviction_lru(void) {
    PeerManager pm;

    // Fill all slots
    for (int i = 0; i < MESH_MAX_PEERS; i++) {
        uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, (uint8_t)i};
        pm.addPeer(mac);
    }
    TEST_ASSERT_EQUAL(MESH_MAX_PEERS, pm.getPeerCount());

    // Add one more — should evict oldest
    uint8_t newMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    PeerEntry* peer = pm.addPeer(newMac);
    TEST_ASSERT_NOT_NULL(peer);
}

void test_remove_peer(void) {
    PeerManager pm;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x05};

    pm.addPeer(mac);
    TEST_ASSERT_EQUAL(1, pm.getPeerCount());

    bool removed = pm.removePeer(mac);
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQUAL(0, pm.getPeerCount());
    TEST_ASSERT_NULL(pm.findPeer(mac));
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_add_and_find_peer);
    RUN_TEST(test_peer_not_found);
    RUN_TEST(test_anti_replay);
    RUN_TEST(test_sequence_tx);
    RUN_TEST(test_set_link_key);
    RUN_TEST(test_eviction_lru);
    RUN_TEST(test_remove_peer);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_add_and_find_peer);
    RUN_TEST(test_peer_not_found);
    RUN_TEST(test_anti_replay);
    RUN_TEST(test_sequence_tx);
    RUN_TEST(test_set_link_key);
    RUN_TEST(test_eviction_lru);
    RUN_TEST(test_remove_peer);
    return UNITY_END();
}
#endif
