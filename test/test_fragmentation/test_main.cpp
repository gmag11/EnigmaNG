/**
 * Unit tests: Fragmentation (fragment / reassemble)
 *
 * Covers:
 *   - Single-fragment round-trip
 *   - Multi-fragment round-trip (in order)
 *   - Multi-fragment round-trip (out of order)
 *   - Partial reassembly returns nullptr
 *   - Payload that exceeds FRAG_MAX_FRAGMENTS * FRAG_MAX_PAYLOAD returns 0
 *   - nextFragId increments monotonically
 */

#include <unity.h>
#include <cstring>
#include "Fragmentation.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t SRC_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

// ---------------------------------------------------------------------------
// Helper: build a fragment payload as Fragmentation::fragment() produces it
// (i.e. already contains the 4-byte frag header + data)
// ---------------------------------------------------------------------------
static void sendFragment(Fragmentation& frag, const Fragmentation::Fragment& f,
                         const uint8_t* srcMac, size_t* outLen) {
    *outLen = 0;
    frag.reassemble(srcMac, f.data, f.length, outLen);
}

// ---------------------------------------------------------------------------
// Test 1: Small payload fits in a single fragment — round-trip
// ---------------------------------------------------------------------------
void test_fragment_single(void) {
    Fragmentation frag;

    const char* msg = "Hello Mesh!";
    size_t msgLen = strlen(msg);

    Fragmentation::Fragment frags[FRAG_MAX_FRAGMENTS];
    uint8_t count = frag.fragment((const uint8_t*)msg, msgLen, 0x0001, frags, FRAG_MAX_FRAGMENTS);
    TEST_ASSERT_EQUAL_MESSAGE(1, count, "Single fragment expected for small payload");

    size_t outLen = 0;
    const uint8_t* result = frag.reassemble(SRC_MAC, frags[0].data, frags[0].length, &outLen);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "Reassembly must succeed after all fragments received");
    TEST_ASSERT_EQUAL(msgLen, outLen);
    TEST_ASSERT_EQUAL_MEMORY(msg, result, msgLen);
}

// ---------------------------------------------------------------------------
// Test 2: Large payload splits into multiple fragments — round-trip in order
// ---------------------------------------------------------------------------
void test_fragment_multiple_in_order(void) {
    Fragmentation frag;

    // Build a payload bigger than one fragment slot
    const size_t payloadLen = FRAG_MAX_PAYLOAD * 3 + 50;
    uint8_t payload[payloadLen];
    for (size_t i = 0; i < payloadLen; i++) payload[i] = (uint8_t)(i & 0xFF);

    Fragmentation::Fragment frags[FRAG_MAX_FRAGMENTS];
    uint8_t count = frag.fragment(payload, payloadLen, 0x0002, frags, FRAG_MAX_FRAGMENTS);
    TEST_ASSERT_EQUAL_MESSAGE(4, count, "Expected 4 fragments");

    // Feed in order — only the last should return the reassembled buffer
    for (uint8_t i = 0; i < count - 1; i++) {
        size_t outLen = 0;
        const uint8_t* partial = frag.reassemble(SRC_MAC, frags[i].data, frags[i].length, &outLen);
        TEST_ASSERT_NULL_MESSAGE(partial, "Partial reassembly must return nullptr");
    }

    size_t outLen = 0;
    const uint8_t* result = frag.reassemble(SRC_MAC, frags[count - 1].data, frags[count - 1].length, &outLen);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "Reassembly must succeed after last fragment");
    TEST_ASSERT_EQUAL(payloadLen, outLen);
    TEST_ASSERT_EQUAL_MEMORY(payload, result, payloadLen);
}

// ---------------------------------------------------------------------------
// Test 3: Multi-fragment round-trip delivered out of order
// ---------------------------------------------------------------------------
void test_fragment_multiple_out_of_order(void) {
    Fragmentation frag;

    const size_t payloadLen = FRAG_MAX_PAYLOAD * 2 + 10;
    uint8_t payload[payloadLen];
    for (size_t i = 0; i < payloadLen; i++) payload[i] = (uint8_t)((i * 3) & 0xFF);

    Fragmentation::Fragment frags[FRAG_MAX_FRAGMENTS];
    uint8_t count = frag.fragment(payload, payloadLen, 0x0003, frags, FRAG_MAX_FRAGMENTS);
    TEST_ASSERT_EQUAL(3, count);

    // Deliver: 2, 0, 1 (last delivered is index 1)
    size_t outLen = 0;
    TEST_ASSERT_NULL(frag.reassemble(SRC_MAC, frags[2].data, frags[2].length, &outLen));
    TEST_ASSERT_NULL(frag.reassemble(SRC_MAC, frags[0].data, frags[0].length, &outLen));

    const uint8_t* result = frag.reassemble(SRC_MAC, frags[1].data, frags[1].length, &outLen);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "Reassembly must succeed when all fragments delivered");
    TEST_ASSERT_EQUAL(payloadLen, outLen);
    TEST_ASSERT_EQUAL_MEMORY(payload, result, payloadLen);
}

// ---------------------------------------------------------------------------
// Test 4: Payload too large (exceeds FRAG_MAX_FRAGMENTS limit) → fragment() returns 0
// ---------------------------------------------------------------------------
void test_fragment_too_large_returns_zero(void) {
    Fragmentation frag;

    const size_t tooLarge = (size_t)(FRAG_MAX_FRAGMENTS + 1) * FRAG_MAX_PAYLOAD;
    uint8_t* payload = new uint8_t[tooLarge]();
    Fragmentation::Fragment frags[FRAG_MAX_FRAGMENTS];

    uint8_t count = frag.fragment(payload, tooLarge, 0x0004, frags, FRAG_MAX_FRAGMENTS);
    TEST_ASSERT_EQUAL_MESSAGE(0, count, "Oversized payload must return 0 fragments");

    delete[] payload;
}

// ---------------------------------------------------------------------------
// Test 5: nextFragId increments on each call
// ---------------------------------------------------------------------------
void test_next_frag_id_increments(void) {
    Fragmentation frag;
    uint16_t id1 = frag.nextFragId();
    uint16_t id2 = frag.nextFragId();
    uint16_t id3 = frag.nextFragId();
    TEST_ASSERT_EQUAL(id1 + 1, id2);
    TEST_ASSERT_EQUAL(id2 + 1, id3);
}

// ---------------------------------------------------------------------------
// Test 6: Duplicate fragment is ignored — reassembly not triggered twice
// ---------------------------------------------------------------------------
void test_duplicate_fragment_ignored(void) {
    Fragmentation frag;

    const size_t payloadLen = FRAG_MAX_PAYLOAD + 10;
    uint8_t payload[payloadLen];
    memset(payload, 0xAB, payloadLen);

    Fragmentation::Fragment frags[FRAG_MAX_FRAGMENTS];
    uint8_t count = frag.fragment(payload, payloadLen, 0x0005, frags, FRAG_MAX_FRAGMENTS);
    TEST_ASSERT_EQUAL(2, count);

    size_t outLen = 0;
    // Send frag 0 twice
    frag.reassemble(SRC_MAC, frags[0].data, frags[0].length, &outLen);
    TEST_ASSERT_NULL(frag.reassemble(SRC_MAC, frags[0].data, frags[0].length, &outLen));

    // Now send frag 1 — should still reassemble correctly
    const uint8_t* result = frag.reassemble(SRC_MAC, frags[1].data, frags[1].length, &outLen);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL(payloadLen, outLen);
    TEST_ASSERT_EQUAL_MEMORY(payload, result, payloadLen);
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_fragment_single);
    RUN_TEST(test_fragment_multiple_in_order);
    RUN_TEST(test_fragment_multiple_out_of_order);
    RUN_TEST(test_fragment_too_large_returns_zero);
    RUN_TEST(test_next_frag_id_increments);
    RUN_TEST(test_duplicate_fragment_ignored);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fragment_single);
    RUN_TEST(test_fragment_multiple_in_order);
    RUN_TEST(test_fragment_multiple_out_of_order);
    RUN_TEST(test_fragment_too_large_returns_zero);
    RUN_TEST(test_next_frag_id_increments);
    RUN_TEST(test_duplicate_fragment_ignored);
    return UNITY_END();
}
#endif
