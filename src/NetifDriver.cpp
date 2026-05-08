#if !defined(ESP8266)

#include "NetifDriver.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_event.h"
#include "lwip/esp_netif_net_stack.h"
#include "lwip/netif.h"
#include "lwip/ip4.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include <cstring>

// ─── lwIP-level netif callbacks ────────────────────────────────────────────────

static err_t mesh_netif_output(struct netif* lwip_netif, struct pbuf* p, const ip4_addr_t* ipaddr) {
    // Called by lwIP when it wants to send an IP packet out through mesh0
    NetifDriver* drv = (NetifDriver*)lwip_netif->state;
    if (!drv) return ERR_IF;

    // Linearize pbuf chain into a contiguous buffer
    uint8_t buf[1500];
    uint16_t len = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    if (len == 0) return ERR_BUF;

    if (drv->txCallback(buf, len)) {
        return ERR_OK;
    }
    return ERR_IF;
}

static err_t mesh_netif_init(struct netif* lwip_netif) {
    lwip_netif->name[0] = 'm';
    lwip_netif->name[1] = '0';
    lwip_netif->output = mesh_netif_output;
    lwip_netif->mtu = 216;  // ESP-NOW payload minus mesh header
    lwip_netif->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

// ─── NetifDriver public API ────────────────────────────────────────────────────

bool NetifDriver::begin(const uint8_t* mac, IPAddress localIP, IPAddress subnet, IPAddress gateway) {
    if (_lwipNetif) return false;  // already initialized

    _lwipNetif = (struct netif*)calloc(1, sizeof(struct netif));
    if (!_lwipNetif) return false;

    _lwipNetif->state = this;

    ip4_addr_t ip, mask, gw;
    ip.addr = (uint32_t)localIP;
    mask.addr = (uint32_t)subnet;
    gw.addr = (uint32_t)gateway;

    // Register our netif with lwIP — must hold TCPIP core lock
    LOCK_TCPIP_CORE();
    struct netif* added = netif_add(_lwipNetif, &ip, &mask, &gw, this, mesh_netif_init, netif_input);
    if (added) {
        // Set MAC on the lwIP netif
        _lwipNetif->hwaddr_len = 6;
        memcpy(_lwipNetif->hwaddr, mac, 6);
        netif_set_up(_lwipNetif);
        netif_set_link_up(_lwipNetif);
    }
    UNLOCK_TCPIP_CORE();

    if (!added) {
        free(_lwipNetif);
        _lwipNetif = nullptr;
        return false;
    }

    return true;
}

void NetifDriver::stop() {
    if (_lwipNetif) {
        LOCK_TCPIP_CORE();
        netif_set_down(_lwipNetif);
        netif_remove(_lwipNetif);
        UNLOCK_TCPIP_CORE();
        free(_lwipNetif);
        _lwipNetif = nullptr;
    }
}

bool NetifDriver::injectRxPacket(const uint8_t* data, size_t len) {
    if (!_lwipNetif) return false;

    struct pbuf* p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!p) return false;

    pbuf_take(p, data, (u16_t)len);

    // tcpip_inpkt dispatches to the lwIP thread safely — no manual locking needed
    if (tcpip_inpkt(p, _lwipNetif, _lwipNetif->input) != ERR_OK) {
        pbuf_free(p);
        return false;
    }
    return true;
}

void NetifDriver::setTxCallback(TxCallback cb, void* ctx) {
    _txCb = cb;
    _txCtx = ctx;
}

bool NetifDriver::txCallback(const uint8_t* data, size_t len) {
    if (_txCb) {
        return _txCb(data, len, _txCtx);
    }
    return false;
}

#endif // !ESP8266
