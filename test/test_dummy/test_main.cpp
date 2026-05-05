#include <unity.h>

// Dummy test to validate the test framework is configured properly
void test_dummy_pass(void) {
    TEST_ASSERT_TRUE(true);
}

void test_dummy_arithmetic(void) {
    TEST_ASSERT_EQUAL(4, 2 + 2);
}

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_dummy_pass);
    RUN_TEST(test_dummy_arithmetic);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_dummy_pass);
    RUN_TEST(test_dummy_arithmetic);
    return UNITY_END();
}
#endif
