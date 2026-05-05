#include <unity.h>
#include "LinkLayer.h"

void setUp(void) {}
void tearDown(void) {}

void test_header_serialize_deserialize(void) {
    MeshFrameHeader original = {};
    original.magic = MESH_MAGIC;
    original.version = MESH_VERSION;
    original.networkId = 0xABCD;
    original.frameType = (uint8_t)FrameType::DATA;
    original.protocol = (uint8_t)Protocol::IPv4;
    original.epoch = 42;
    memset(original.srcMac, 0x11, 6);
    memset(original.dstMac, 0x22, 6);
    original.sequence = 0x1234;

    uint8_t buf[MESH_HEADER_SIZE];
    size_t written = LinkLayer::serializeHeader(buf, original);
    TEST_ASSERT_EQUAL(MESH_HEADER_SIZE, written);

    MeshFrameHeader decoded = {};
    bool ok = LinkLayer::deserializeHeader(buf, sizeof(buf), decoded);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_HEX16(original.magic, decoded.magic);
    TEST_ASSERT_EQUAL(original.version, decoded.version);
    TEST_ASSERT_EQUAL_HEX16(original.networkId, decoded.networkId);
    TEST_ASSERT_EQUAL(original.frameType, decoded.frameType);
    TEST_ASSERT_EQUAL(original.protocol, decoded.protocol);
    TEST_ASSERT_EQUAL(original.epoch, decoded.epoch);
    TEST_ASSERT_EQUAL_MEMORY(original.srcMac, decoded.srcMac, 6);
    TEST_ASSERT_EQUAL_MEMORY(original.dstMac, decoded.dstMac, 6);
    TEST_ASSERT_EQUAL_HEX16(original.sequence, decoded.sequence);
}

void test_header_validate_magic(void) {
    MeshFrameHeader good = {};
    good.magic = MESH_MAGIC;
    good.version = MESH_VERSION;
    good.frameType = (uint8_t)FrameType::DATA;
    TEST_ASSERT_TRUE(LinkLayer::validateHeader(good));

    MeshFrameHeader badMagic = good;
    badMagic.magic = 0x0000;
    TEST_ASSERT_FALSE(LinkLayer::validateHeader(badMagic));

    MeshFrameHeader badVersion = good;
    badVersion.version = 0xFF;
    TEST_ASSERT_FALSE(LinkLayer::validateHeader(badVersion));
}

void test_network_id_filter(void) {
    MeshFrameHeader header = {};
    header.networkId = 0xBEEF;

    TEST_ASSERT_TRUE(LinkLayer::matchesNetwork(header, 0xBEEF));
    TEST_ASSERT_FALSE(LinkLayer::matchesNetwork(header, 0xDEAD));
}

void test_header_too_short(void) {
    uint8_t buf[10] = {};
    MeshFrameHeader header = {};
    TEST_ASSERT_FALSE(LinkLayer::deserializeHeader(buf, 10, header));
}

void test_frag_header_serialize_deserialize(void) {
    MeshFragHeader original = {};
    original.fragId = 0x1234;
    original.fragIndex = 2;
    original.fragTotal = 5;

    uint8_t buf[MESH_FRAG_HEADER_SIZE];
    size_t written = LinkLayer::serializeFragHeader(buf, original);
    TEST_ASSERT_EQUAL(MESH_FRAG_HEADER_SIZE, written);

    MeshFragHeader decoded = {};
    bool ok = LinkLayer::deserializeFragHeader(buf, sizeof(buf), decoded);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_HEX16(original.fragId, decoded.fragId);
    TEST_ASSERT_EQUAL(original.fragIndex, decoded.fragIndex);
    TEST_ASSERT_EQUAL(original.fragTotal, decoded.fragTotal);
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_header_serialize_deserialize);
    RUN_TEST(test_header_validate_magic);
    RUN_TEST(test_network_id_filter);
    RUN_TEST(test_header_too_short);
    RUN_TEST(test_frag_header_serialize_deserialize);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_header_serialize_deserialize);
    RUN_TEST(test_header_validate_magic);
    RUN_TEST(test_network_id_filter);
    RUN_TEST(test_header_too_short);
    RUN_TEST(test_frag_header_serialize_deserialize);
    return UNITY_END();
}
#endif
