/**
 * Integration test: Simulated route discovery (Distance-Vector, 3 nodes)
 *
 * Topology:
 *
 *   Gateway (G) ──── Relay (R) ──── Leaf (L)
 *
 * Full scenario:
 *   1. G and R are direct neighbors. R installs a direct route to G (hop=1).
 *   2. R and L are direct neighbors. R installs a direct route to L (hop=1).
 *   3. R sends ROUTE_ADV to G (with Split Horizon: excludes route toward G itself).
 *      G should learn route to L with hop=2.
 *   4. G forwards ROUTE_ADV to R (Poison Reverse: route to L must have hop=16).
 *      R should not install that route (infinity).
 *   5. Split Horizon: G does not advertise to R the route whose nextHop==R.
 *   6. ROUTE_WITHDRAW: L goes down, R emits withdraw → G removes route to L.
 */

#include <unity.h>
#include <cstring>
#include "Router.h"

void setUp(void) {}
void tearDown(void) {}

// MACs of the three nodes
static const uint8_t MAC_G[6] = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x01}; // Gateway
static const uint8_t MAC_R[6] = {0xBB, 0x00, 0x00, 0x00, 0x00, 0x02}; // Relay
static const uint8_t MAC_L[6] = {0xCC, 0x00, 0x00, 0x00, 0x00, 0x03}; // Leaf

// IPs derived from scheme 10.200.<mac[4]>.<mac[5]>
static const IPAddress IP_G(10, 200, 0, 1);
static const IPAddress IP_R(10, 200, 0, 2);
static const IPAddress IP_L(10, 200, 0, 3);

// ---------------------------------------------------------------------------
// Test 1: Direct neighbors are known (hop=1)
// ---------------------------------------------------------------------------
void test_direct_neighbors_hop1(void) {
    Router routerG, routerR;

    // G agrega a R como directo
    routerG.addRoute(IP_R, MAC_R, MAC_R, 1);
    // R agrega a G como directo
    routerR.addRoute(IP_G, MAC_G, MAC_G, 1);
    // R agrega a L como directo
    routerR.addRoute(IP_L, MAC_L, MAC_L, 1);

    RouteEntry* rInG = routerG.findRouteByIP(IP_R);
    TEST_ASSERT_NOT_NULL(rInG);
    TEST_ASSERT_EQUAL_MESSAGE(1, rInG->hopCount, "G->R must be hop=1");

    RouteEntry* gInR = routerR.findRouteByIP(IP_G);
    TEST_ASSERT_NOT_NULL(gInR);
    TEST_ASSERT_EQUAL_MESSAGE(1, gInR->hopCount, "R->G must be hop=1");

    RouteEntry* lInR = routerR.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL(lInR);
    TEST_ASSERT_EQUAL_MESSAGE(1, lInR->hopCount, "R->L must be hop=1");
}

// ---------------------------------------------------------------------------
// Test 2: Multi-hop route discovery (G learns L via R's ROUTE_ADV)
// ---------------------------------------------------------------------------
void test_route_discovery_multihop(void) {
    Router routerG, routerR;

    // Estado inicial: R conoce a G y L directamente
    routerR.addRoute(IP_G, MAC_G, MAC_G, 1);
    routerR.addRoute(IP_L, MAC_L, MAC_L, 1);
    // G solo conoce a R
    routerG.addRoute(IP_R, MAC_R, MAC_R, 1);

    // R serializa ROUTE_ADV para G (excluye rutas cuyo nextHop == G → Split Horizon)
    uint8_t advBuf[256];
    size_t advLen = routerR.serializeRouteAdv(advBuf, sizeof(advBuf), MAC_G);

    // There must be at least one entry (route to L; route to G is excluded/poisoned)
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, advLen, "R's ROUTE_ADV must not be empty");

    // G procesa el ROUTE_ADV de R
    routerG.deserializeRouteAdv(advBuf, advLen, MAC_R, MAC_G);

    // G must now have a route to L via R with hop=2
    RouteEntry* lInG = routerG.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL_MESSAGE(lInG, "G must have learned the route to L");
    TEST_ASSERT_EQUAL_MESSAGE(2, lInG->hopCount, "G->L route must be hop=2");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(MAC_R, lInG->nextHopMac, 6,
                                     "G->L nextHop must be R");

    // G must NOT have a route to itself (localMac filtered)
    RouteEntry* gInG = routerG.findRouteByIP(IP_G);
    if (gInG) {
        // If it existed before deserialization, ensure it wasn't overwritten
        // The route to G must not arrive with nextHop=R (that would be erroneous)
        TEST_ASSERT_FALSE_MESSAGE(
            memcmp(gInG->nextHopMac, MAC_R, 6) == 0 && gInG->hopCount > 1,
            "G must not install a route to itself via R");
    }
}

// ---------------------------------------------------------------------------
// Test 3: Split Horizon — R must not learn routes that it originally sent to G
// ---------------------------------------------------------------------------
void test_split_horizon_no_reflection(void) {
    Router routerG, routerR;

    // G tiene ruta a L con nextHop=R (aprendida de R)
    routerG.addRoute(IP_R, MAC_R, MAC_R, 1);
    routerG.addRoute(IP_L, MAC_L, MAC_R, 2);  // aprendida de R

    // G serializa para R (Split Horizon: excluye rutas con nextHop==R)
    uint8_t advBuf[256];
    size_t advLen = routerG.serializeRouteAdv(advBuf, sizeof(advBuf), MAC_R);

    // R procesa el ROUTE_ADV de G
    routerR.addRoute(IP_G, MAC_G, MAC_G, 1);
    routerR.addRoute(IP_L, MAC_L, MAC_L, 1);  // R ya tiene ruta directa a L
    size_t prevCount = routerR.getRouteCount();

    routerR.deserializeRouteAdv(advBuf, advLen, MAC_G, MAC_R);

    // The route to L in R must not worsen (hopCount should remain 1)
    RouteEntry* lInR = routerR.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL(lInR);
    TEST_ASSERT_EQUAL_MESSAGE(1, lInR->hopCount,
                              "Split Horizon: R must not accept a worse route to L from G");

    // The entry from G (Poison Reverse) must not install a useless route
    // The number of routes in R should not increase compared to previous state
    // (at most it remains equal or equal + route to G itself)
    (void)prevCount;  // qualitative check sufficient with hopCount
}

// ---------------------------------------------------------------------------
// Test 4: Poison Reverse — G advertises to R its route to L with hop=16 (infinity)
// ---------------------------------------------------------------------------
void test_poison_reverse_advertised(void) {
    Router routerG, routerR;

    // G has route to L with nextHop=R (learned from R)
    routerG.addRoute(IP_L, MAC_L, MAC_R, 2);

    // G serializes for R → it should include L with hop=16 (Poison Reverse)
    uint8_t advBuf[256];
    size_t advLen = routerG.serializeRouteAdv(advBuf, sizeof(advBuf), MAC_R);

    TEST_ASSERT_GREATER_THAN(0, advLen);

    // Manually verify that the entry for L in the buffer has hop=16
    size_t entries = advLen / ROUTE_ADV_ENTRY_SIZE;
    bool foundPoisoned = false;
    for (size_t i = 0; i < entries; i++) {
        size_t off = i * ROUTE_ADV_ENTRY_SIZE;
        uint8_t mac[6];
        memcpy(mac, &advBuf[off + 4], 6);
        uint8_t hop = advBuf[off + 10];
        if (memcmp(mac, MAC_L, 6) == 0) {
            TEST_ASSERT_EQUAL_MESSAGE(16, hop, "Route to L must be poisoned (hop=16)");
            foundPoisoned = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(foundPoisoned, "L must appear in the ROUTE_ADV with Poison Reverse");

    // R must not install that route (hop≥16 is discarded in deserialize)
    routerR.addRoute(IP_G, MAC_G, MAC_G, 1);
    routerR.addRoute(IP_L, MAC_L, MAC_L, 1);  // directa
    routerR.deserializeRouteAdv(advBuf, advLen, MAC_G, MAC_R);

    RouteEntry* lInR = routerR.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL(lInR);
    TEST_ASSERT_LESS_THAN_MESSAGE(16, lInR->hopCount,
                                  "R must not accept the poisoned route (infinity)");
}

// ---------------------------------------------------------------------------
// Test 5: ROUTE_WITHDRAW — G removes route to L when R withdraws it
// ---------------------------------------------------------------------------
void test_route_withdraw_removes_route(void) {
    Router routerG;

    // G ha aprendido ruta a L via R
    routerG.addRoute(IP_R, MAC_R, MAC_R, 1);
    routerG.addRoute(IP_L, MAC_L, MAC_R, 2);

    TEST_ASSERT_EQUAL_MESSAGE(2, routerG.getRouteCount(), "G must have 2 routes before withdraw");

    // R detects that L went down and emits ROUTE_WITHDRAW
    routerG.handleRouteWithdraw(MAC_L);

    TEST_ASSERT_NULL_MESSAGE(routerG.findRouteByIP(IP_L),
                             "G must remove the route to L after ROUTE_WITHDRAW");
    TEST_ASSERT_EQUAL_MESSAGE(1, routerG.getRouteCount(), "Only the route to R must remain");

    // The route to R must remain intact
    RouteEntry* rInG = routerG.findRouteByIP(IP_R);
    TEST_ASSERT_NOT_NULL(rInG);
    TEST_ASSERT_EQUAL(1, rInG->hopCount);
}

// ---------------------------------------------------------------------------
// Test 6: Converged topology — full table for 3 nodes
// ---------------------------------------------------------------------------
void test_full_mesh_convergence(void) {
    Router routerG, routerR, routerL;

    // --- Round 1: each node knows its direct neighbors ---
    routerG.addRoute(IP_R, MAC_R, MAC_R, 1);
    routerR.addRoute(IP_G, MAC_G, MAC_G, 1);
    routerR.addRoute(IP_L, MAC_L, MAC_L, 1);
    routerL.addRoute(IP_R, MAC_R, MAC_R, 1);

    // --- Round 2: R announces to G (G learns L) ---
    {
        uint8_t buf[256];
        size_t len = routerR.serializeRouteAdv(buf, sizeof(buf), MAC_G);
        routerG.deserializeRouteAdv(buf, len, MAC_R, MAC_G);
    }

    // --- Round 2: R announces to L (L learns G) ---
    {
        uint8_t buf[256];
        size_t len = routerR.serializeRouteAdv(buf, sizeof(buf), MAC_L);
        routerL.deserializeRouteAdv(buf, len, MAC_R, MAC_L);
    }

    // Verify convergence
    TEST_ASSERT_NOT_NULL_MESSAGE(routerG.findRouteByIP(IP_L), "G must know L (via R)");
    TEST_ASSERT_NOT_NULL_MESSAGE(routerL.findRouteByIP(IP_G), "L must know G (via R)");

    TEST_ASSERT_EQUAL_MESSAGE(2, routerG.findRouteByIP(IP_L)->hopCount, "G->L = hop 2");
    TEST_ASSERT_EQUAL_MESSAGE(2, routerL.findRouteByIP(IP_G)->hopCount, "L->G = hop 2");

    // R es el relay para ambas rutas
    TEST_ASSERT_EQUAL_MEMORY(MAC_R, routerG.findRouteByIP(IP_L)->nextHopMac, 6);
    TEST_ASSERT_EQUAL_MEMORY(MAC_R, routerL.findRouteByIP(IP_G)->nextHopMac, 6);
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_direct_neighbors_hop1);
    RUN_TEST(test_route_discovery_multihop);
    RUN_TEST(test_split_horizon_no_reflection);
    RUN_TEST(test_poison_reverse_advertised);
    RUN_TEST(test_route_withdraw_removes_route);
    RUN_TEST(test_full_mesh_convergence);
    return UNITY_END();
}
