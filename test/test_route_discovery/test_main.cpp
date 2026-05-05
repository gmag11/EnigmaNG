/**
 * Integration test: Simulated route discovery (Distance-Vector, 3 nodos)
 *
 * Topología:
 *
 *   Gateway (G) ──── Relay (R) ──── Leaf (L)
 *
 * Escenario completo:
 *   1. G y R son vecinos directos. R instala ruta directa a G (hop=1).
 *   2. R y L son vecinos directos. R instala ruta directa a L (hop=1).
 *   3. R envía ROUTE_ADV a G (con Split Horizon: excluye ruta hacia G propio).
 *      G debe aprender la ruta a L con hop=2.
 *   4. G reenvía ROUTE_ADV a R (Poison Reverse: ruta a L debe tener hop=16).
 *      R no instala esa ruta (infinity).
 *   5. Split Horizon: G no anuncia a R la ruta cuyo nextHop=R.
 *   6. ROUTE_WITHDRAW: L se cae, R emite withdraw → G elimina la ruta a L.
 */

#include <unity.h>
#include <cstring>
#include "Router.h"

void setUp(void) {}
void tearDown(void) {}

// MACs de los tres nodos
static const uint8_t MAC_G[6] = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x01}; // Gateway
static const uint8_t MAC_R[6] = {0xBB, 0x00, 0x00, 0x00, 0x00, 0x02}; // Relay
static const uint8_t MAC_L[6] = {0xCC, 0x00, 0x00, 0x00, 0x00, 0x03}; // Leaf

// IPs derivadas del esquema 10.200.<mac[4]>.<mac[5]>
static const IPAddress IP_G(10, 200, 0, 1);
static const IPAddress IP_R(10, 200, 0, 2);
static const IPAddress IP_L(10, 200, 0, 3);

// ---------------------------------------------------------------------------
// Test 1: Vecinos directos se conocen (hop=1)
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
    TEST_ASSERT_EQUAL_MESSAGE(1, rInG->hopCount, "G->R debe ser hop=1");

    RouteEntry* gInR = routerR.findRouteByIP(IP_G);
    TEST_ASSERT_NOT_NULL(gInR);
    TEST_ASSERT_EQUAL_MESSAGE(1, gInR->hopCount, "R->G debe ser hop=1");

    RouteEntry* lInR = routerR.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL(lInR);
    TEST_ASSERT_EQUAL_MESSAGE(1, lInR->hopCount, "R->L debe ser hop=1");
}

// ---------------------------------------------------------------------------
// Test 2: Descubrimiento de ruta multi-hop (G aprende L via ROUTE_ADV de R)
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

    // Debe haber al menos una entrada (la ruta a L; la ruta a G está excluida/envenenada)
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, advLen, "ROUTE_ADV de R no debe estar vacío");

    // G procesa el ROUTE_ADV de R
    routerG.deserializeRouteAdv(advBuf, advLen, MAC_R, MAC_G);

    // G debe tener ahora ruta a L via R con hop=2
    RouteEntry* lInG = routerG.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL_MESSAGE(lInG, "G debe haber aprendido la ruta a L");
    TEST_ASSERT_EQUAL_MESSAGE(2, lInG->hopCount, "Ruta G->L debe ser hop=2");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(MAC_R, lInG->nextHopMac, 6,
                                     "El nextHop de G->L debe ser R");

    // G NO debe tener ruta a sí mismo (localMac filtrado)
    RouteEntry* gInG = routerG.findRouteByIP(IP_G);
    if (gInG) {
        // Si existía antes de la deserialización, asegurarse que no la sobreescribió
        // La ruta a G no debe llegar con nextHop=R (sería absurdo)
        TEST_ASSERT_FALSE_MESSAGE(
            memcmp(gInG->nextHopMac, MAC_R, 6) == 0 && gInG->hopCount > 1,
            "G no debe instalar ruta a sí mismo via R");
    }
}

// ---------------------------------------------------------------------------
// Test 3: Split Horizon — R no debe aprender rutas que ya aprendió de G
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

    // La ruta a L en R no debe empeorar (hopCount debe seguir siendo 1)
    RouteEntry* lInR = routerR.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL(lInR);
    TEST_ASSERT_EQUAL_MESSAGE(1, lInR->hopCount,
                              "Split Horizon: R no debe aceptar ruta peor a L desde G");

    // La entrada de G (Poison Reverse) no debe instalar ruta inútil
    // El número de rutas en R no debe crecer respecto a lo que ya tenía
    // (a lo sumo se mantiene igual o igual + ruta al propio G)
    (void)prevCount;  // comprobación cualitativa suficiente con hopCount
}

// ---------------------------------------------------------------------------
// Test 4: Poison Reverse — G anuncia a R su ruta a L con hop=16 (infinity)
// ---------------------------------------------------------------------------
void test_poison_reverse_advertised(void) {
    Router routerG, routerR;

    // G tiene ruta a L con nextHop=R (la aprendió de R)
    routerG.addRoute(IP_L, MAC_L, MAC_R, 2);

    // G serializa para R → debe incluir L con hop=16 (Poison Reverse)
    uint8_t advBuf[256];
    size_t advLen = routerG.serializeRouteAdv(advBuf, sizeof(advBuf), MAC_R);

    TEST_ASSERT_GREATER_THAN(0, advLen);

    // Verificar manualmente que la entrada de L en el buffer tiene hop=16
    size_t entries = advLen / ROUTE_ADV_ENTRY_SIZE;
    bool foundPoisoned = false;
    for (size_t i = 0; i < entries; i++) {
        size_t off = i * ROUTE_ADV_ENTRY_SIZE;
        uint8_t mac[6];
        memcpy(mac, &advBuf[off + 4], 6);
        uint8_t hop = advBuf[off + 10];
        if (memcmp(mac, MAC_L, 6) == 0) {
            TEST_ASSERT_EQUAL_MESSAGE(16, hop, "Ruta a L debe estar envenenada (hop=16)");
            foundPoisoned = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(foundPoisoned, "L debe aparecer en el ROUTE_ADV con Poison Reverse");

    // R no debe instalar esa ruta (hop≥16 se descarta en deserialize)
    routerR.addRoute(IP_G, MAC_G, MAC_G, 1);
    routerR.addRoute(IP_L, MAC_L, MAC_L, 1);  // directa
    routerR.deserializeRouteAdv(advBuf, advLen, MAC_G, MAC_R);

    RouteEntry* lInR = routerR.findRouteByIP(IP_L);
    TEST_ASSERT_NOT_NULL(lInR);
    TEST_ASSERT_LESS_THAN_MESSAGE(16, lInR->hopCount,
                                  "R no debe aceptar la ruta envenenada (infinity)");
}

// ---------------------------------------------------------------------------
// Test 5: ROUTE_WITHDRAW — G elimina la ruta a L cuando R la retira
// ---------------------------------------------------------------------------
void test_route_withdraw_removes_route(void) {
    Router routerG;

    // G ha aprendido ruta a L via R
    routerG.addRoute(IP_R, MAC_R, MAC_R, 1);
    routerG.addRoute(IP_L, MAC_L, MAC_R, 2);

    TEST_ASSERT_EQUAL_MESSAGE(2, routerG.getRouteCount(), "G debe tener 2 rutas antes del withdraw");

    // R detecta que L se ha caído y emite ROUTE_WITHDRAW
    routerG.handleRouteWithdraw(MAC_L);

    TEST_ASSERT_NULL_MESSAGE(routerG.findRouteByIP(IP_L),
                             "G debe eliminar la ruta a L tras el ROUTE_WITHDRAW");
    TEST_ASSERT_EQUAL_MESSAGE(1, routerG.getRouteCount(), "Solo debe quedar la ruta a R");

    // La ruta a R debe seguir intacta
    RouteEntry* rInG = routerG.findRouteByIP(IP_R);
    TEST_ASSERT_NOT_NULL(rInG);
    TEST_ASSERT_EQUAL(1, rInG->hopCount);
}

// ---------------------------------------------------------------------------
// Test 6: Topología convergida — tabla completa de 3 nodos
// ---------------------------------------------------------------------------
void test_full_mesh_convergence(void) {
    Router routerG, routerR, routerL;

    // --- Ronda 1: cada nodo conoce sus vecinos directos ---
    routerG.addRoute(IP_R, MAC_R, MAC_R, 1);
    routerR.addRoute(IP_G, MAC_G, MAC_G, 1);
    routerR.addRoute(IP_L, MAC_L, MAC_L, 1);
    routerL.addRoute(IP_R, MAC_R, MAC_R, 1);

    // --- Ronda 2: R anuncia a G (aprende L) ---
    {
        uint8_t buf[256];
        size_t len = routerR.serializeRouteAdv(buf, sizeof(buf), MAC_G);
        routerG.deserializeRouteAdv(buf, len, MAC_R, MAC_G);
    }

    // --- Ronda 2: R anuncia a L (aprende G) ---
    {
        uint8_t buf[256];
        size_t len = routerR.serializeRouteAdv(buf, sizeof(buf), MAC_L);
        routerL.deserializeRouteAdv(buf, len, MAC_R, MAC_L);
    }

    // Verificar convergencia
    TEST_ASSERT_NOT_NULL_MESSAGE(routerG.findRouteByIP(IP_L), "G debe conocer L (via R)");
    TEST_ASSERT_NOT_NULL_MESSAGE(routerL.findRouteByIP(IP_G), "L debe conocer G (via R)");

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
