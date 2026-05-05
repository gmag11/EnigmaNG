#ifndef MESH_SERVICE_DISCOVERY_H
#define MESH_SERVICE_DISCOVERY_H

#include <Arduino.h>
#include <IPAddress.h>

#define MAX_SERVICES 8
#define SERVICE_NAME_MAX 16

struct MeshService {
    char name[SERVICE_NAME_MAX];  // e.g., "_mqtt._tcp"
    IPAddress ip;
    uint16_t port;
    uint8_t  nodeMac[6];
    uint32_t lastSeen;
    bool valid;
};

class ServiceDiscovery {
public:
    // Register a local service
    bool registerService(const char* name, uint16_t port);
    bool unregisterService(const char* name);

    // Query for a service in the mesh
    bool queryService(const char* name);

    // Handle incoming SERVICE_QUERY / SERVICE_REPLY
    void handleServiceQuery(const uint8_t* fromMac, const char* name);
    void handleServiceReply(const uint8_t* fromMac, const char* name, IPAddress ip, uint16_t port);

    // Get discovered service
    const MeshService* findService(const char* name);

    // mDNS bridge (gateway only): republish mesh services to WiFi LAN
    bool startMdnsBridge();

    // Embed services in ROUTE_ADV (optional field)
    size_t serializeServices(uint8_t* buf, size_t bufLen);
    void deserializeServices(const uint8_t* buf, size_t len, const uint8_t* fromMac);

    void update();

private:
    MeshService _localServices[MAX_SERVICES] = {};
    MeshService _discoveredServices[MAX_SERVICES] = {};
    size_t _localCount = 0;
    size_t _discoveredCount = 0;
};

#endif // MESH_SERVICE_DISCOVERY_H
