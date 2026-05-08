#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

// ═══════════════════════════════════════════════════════════════════════
// meshConfig.h — EnigmaNG tuneable parameters
//
// Override any of these by defining the symbol before including this
// header (e.g. via build_flags in platformio.ini).
// ═══════════════════════════════════════════════════════════════════════

// ── Peer table ────────────────────────────────────────────────────────
// Maximum number of simultaneously tracked peers
#ifndef MESH_MAX_PEERS
#define MESH_MAX_PEERS              16
#endif

// ── Routing table ─────────────────────────────────────────────────────
// Maximum number of route entries
#ifndef MESH_MAX_ROUTES
#define MESH_MAX_ROUTES             64
#endif

// Size of the seen-frame deduplication cache
#ifndef MESH_SEEN_CACHE_SIZE
#define MESH_SEEN_CACHE_SIZE        32
#endif

// Maximum number of tracked gateway candidates
#ifndef MESH_MAX_GATEWAYS
#define MESH_MAX_GATEWAYS           4
#endif

// ── Gateway / NAT (ESP32 only) ────────────────────────────────────────
// Enable lwIP IP forwarding
#ifndef CONFIG_LWIP_IP_FORWARD
#define CONFIG_LWIP_IP_FORWARD      1
#endif

// Enable lwIP IPv4 NAPT
#ifndef CONFIG_LWIP_IPV4_NAPT
#define CONFIG_LWIP_IPV4_NAPT       1
#endif

// ── MeshNode8266 (ESP8266 proxy node) ────────────────────────────────
// Maximum MQTT topic length (bytes, including NUL)
#ifndef MESH8266_MAX_TOPIC_LEN
#define MESH8266_MAX_TOPIC_LEN      64
#endif

// Maximum number of stored MQTT subscriptions
#ifndef MESH8266_MAX_SUBSCRIPTIONS
#define MESH8266_MAX_SUBSCRIPTIONS  8
#endif

// Time to collect PROXY_OFFER responses before selecting best proxy (ms)
#ifndef MESH8266_DISCOVERY_TIMEOUT_MS
#define MESH8266_DISCOVERY_TIMEOUT_MS 3000
#endif

// Maximum number of proxy offers to store during discovery
#ifndef MESH8266_MAX_PROXY_OFFERS
#define MESH8266_MAX_PROXY_OFFERS   4
#endif

#endif // MESH_CONFIG_H
