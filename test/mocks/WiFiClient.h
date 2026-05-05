#pragma once
// WiFiClient.h stub for native unit test builds.
struct WiFiClient {
    bool connected() { return false; }
    void stop() {}
};
