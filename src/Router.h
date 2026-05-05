#ifndef MESH_ROUTER_H
#define MESH_ROUTER_H

#include <Arduino.h>
#include <IPAddress.h>

#ifndef MESH_MAX_ROUTES
#define MESH_MAX_ROUTES 64
#endif

#ifndef MESH_SEEN_CACHE_SIZE
#define MESH_SEEN_CACHE_SIZE 32
#endif

#define ROUTE_ADV_INTERVAL_MS       30000   // 30s
#define ROUTE_EXPIRE_MS             90000   // 3 × interval
#define ROUTE_ADV_ENTRY_SIZE        12      // Bytes per entry in ROUTE_ADV frame
#define ROUTE_ADV_MAX_ENTRIES       18      // Max entries per frame (18 × 12 = 216)
#define SEEN_FRAME_TTL_MS           10000   // 10s

// Route entry: ~25 bytes
struct RouteEntry {
    IPAddress destIP;        // 4 bytes
    uint8_t   destMac[6];   // 6 bytes
    uint8_t   nextHopMac[6];// 6 bytes  (direct neighbor MAC for forwarding)
    uint8_t   hopCount;     // 1 byte
    uint8_t   metric;       // 1 byte  (composite: hopCount + RSSI factor)
    uint32_t  lastUpdated;  // 4 bytes (millis)
    bool      valid;        // 1 byte
};

// Seen-Frame Cache entry (anti-loop)
struct SeenFrameEntry {
    uint8_t  srcMac[6];
    uint16_t sequence;
    uint32_t timestamp;
    bool     valid;
};

class Router {
public:
    Router();

    // Route table management
    RouteEntry* addRoute(IPAddress destIP, const uint8_t* destMac,
                         const uint8_t* nextHopMac, uint8_t hopCount);
    RouteEntry* findRouteByIP(IPAddress destIP);
    RouteEntry* findRouteByMac(const uint8_t* destMac);
    bool removeRoute(IPAddress destIP);
    void expireRoutes();

    // ROUTE_ADV serialization (12 bytes per entry)
    size_t serializeRouteAdv(uint8_t* buf, size_t bufLen, const uint8_t* excludeNextHop);
    size_t deserializeRouteAdv(const uint8_t* buf, size_t len, const uint8_t* fromMac);

    // ROUTE_WITHDRAW
    void handleRouteWithdraw(const uint8_t* destMac);

    // Seen-Frame Cache (duplicate detection for broadcast/relay)
    bool isFrameSeen(const uint8_t* srcMac, uint16_t seq);
    void markFrameSeen(const uint8_t* srcMac, uint16_t seq);

    // Split Horizon: should we advertise this route to neighbor?
    bool shouldAdvertiseTo(const RouteEntry& route, const uint8_t* neighborMac);

    // Triggered update detection
    bool hasTopologyChanged() const;
    void clearTopologyChanged();

    // Statistics
    size_t getRouteCount() const;
    RouteEntry* getRouteByIndex(size_t index);

    // Periodic update
    void update();

private:
    RouteEntry _routes[MESH_MAX_ROUTES];
    size_t _routeCount = 0;
    bool _topologyChanged = false;

    // Seen-Frame Cache (circular buffer)
    SeenFrameEntry _seenCache[MESH_SEEN_CACHE_SIZE];
    size_t _seenCacheIdx = 0;

    // Eviction: remove worst route when table is full
    int _findEvictionCandidate();
};

#endif // MESH_ROUTER_H
