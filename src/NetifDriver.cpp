#if !defined(ESP8266)

#include "NetifDriver.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/esp_netif_net_stack.h"  // full definition of esp_netif_netstack_config_t (.lwip union)
#include "esp_event.h"
#include "lwip/netif.h"
#include "lwip/ip4.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include "lwip/dns.h"
#include <cstring>

// --- esp_netif custom driver ---
//
// In IDF 5.x, every lwIP netif registered via netif_add() triggers the global
// esp_netif_internal_dhcpc_cb() ext-callback, which reads netif->state as
// esp_netif_t*. If the netif was added directly with netif_add() (bypassing
// esp_netif), state is our own pointer and the cast produces garbage -> crash.
//
// The fix is to create mesh0 through esp_netif_new() + esp_netif_attach() so
// that state is correctly set to a valid esp_netif_t* by the IDF layer.

// Driver handle -- must start with esp_netif_driver_base_t (IDF requirement)
struct MeshNetifDriverHandle {
    esp_netif_driver_base_t base;  // MUST be first member
    NetifDriver*            drv;
};

// --- lwIP output function ---
// After esp_netif_attach(), netif->state is esp_netif_t*.
// We route the packet through esp_netif_transmit() -> our driver transmit fn.

static err_t mesh_netif_output(struct netif* lwip_netif, struct pbuf* p,
                               const ip4_addr_t* /*ipaddr*/) {
    uint8_t buf[1500];
    uint16_t len = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    if (len == 0) return ERR_BUF;

    esp_netif_t* esp_netif = static_cast<esp_netif_t*>(lwip_netif->state);
    esp_err_t err = esp_netif_transmit(esp_netif, buf, len);
    return (err == ESP_OK) ? ERR_OK : ERR_IF;
}

// --- lwIP netif init callback ---

static err_t mesh_lwip_init(struct netif* lwip_netif) {
    lwip_netif->name[0] = 'm';
    lwip_netif->name[1] = '0';
    lwip_netif->output  = mesh_netif_output;
    lwip_netif->mtu     = 216;  // ESP-NOW payload minus mesh header
    // No NETIF_FLAG_ETHARP / NETIF_FLAG_ETHERNET -- this is a raw-IP interface.
    // BROADCAST is needed so lwIP participates in subnet-level broadcasts.
    // Flags UP + LINK_UP are set by esp_netif_action_start/connected.
    lwip_netif->flags |= NETIF_FLAG_BROADCAST;
    return ERR_OK;
}

// --- Custom input function ---
// IDF's esp_netif_config_sanity_check() requires lwip_input_fn != NULL, so we
// must provide one.  In practice injectRxPacket() calls tcpip_input() directly
// (with PBUF_LINK headroom) and never goes through esp_netif_receive(), so this
// function is a correct-but-unused fallback.
static esp_netif_recv_ret_t mesh_netif_input(void* h, void* buffer, size_t len,
                                             void* /*eb*/) {
    struct netif* lwip_netif = static_cast<struct netif*>(h);
    if (!lwip_netif || !buffer || len == 0) {
        ESP_NETIF_OPTIONAL_RETURN_CODE(return ESP_ERR_INVALID_ARG);
    }
    struct pbuf* p = pbuf_alloc(PBUF_LINK, static_cast<u16_t>(len), PBUF_POOL);
    if (!p) {
        ESP_NETIF_OPTIONAL_RETURN_CODE(return ESP_ERR_NO_MEM);
    }
    pbuf_take(p, buffer, static_cast<u16_t>(len));
    if (lwip_netif->input(p, lwip_netif) != ERR_OK) {
        pbuf_free(p);
        ESP_NETIF_OPTIONAL_RETURN_CODE(return ESP_FAIL);
    }
    ESP_NETIF_OPTIONAL_RETURN_CODE(return ESP_OK);
}

// --- Custom netstack config (raw IP, no Ethernet framing, no DHCP) ---

static const esp_netif_netstack_config_t s_mesh_netstack = {
    .lwip = {
        .init_fn  = mesh_lwip_init,
        .input_fn = mesh_netif_input,
    }
};

// --- Driver callbacks ---

static esp_err_t mesh_drv_transmit(void* h, void* buffer, size_t len) {
    auto* handle = static_cast<MeshNetifDriverHandle*>(h);
    return handle->drv->txCallback(static_cast<const uint8_t*>(buffer), len)
               ? ESP_OK : ESP_FAIL;
}

static esp_err_t mesh_drv_post_attach(esp_netif_t* esp_netif, void* args) {
    auto* handle = static_cast<MeshNetifDriverHandle*>(args);
    handle->base.netif = esp_netif;
    const esp_netif_driver_ifconfig_t driver_cfg = {
        .handle              = handle,
        .transmit            = mesh_drv_transmit,
        .driver_free_rx_buffer = nullptr,
    };
    return esp_netif_set_driver_config(esp_netif, &driver_cfg);
}

// --- NetifDriver public API ---

bool NetifDriver::begin(const uint8_t* mac, IPAddress localIP, IPAddress subnet,
                        IPAddress gateway) {
    if (_espNetif) return false;  // already initialized

    // Inherent config: plain IP interface, no DHCP client, no DHCP server.
    // flags = 0 ensures esp_netif_internal_dhcpc_cb() is a safe no-op for this
    // netif (the callback checks ESP_NETIF_DHCP_CLIENT which we never set).
    esp_netif_inherent_config_t base_cfg = {};
    base_cfg.flags         = static_cast<esp_netif_flags_t>(0);
    base_cfg.get_ip_event  = 0;
    base_cfg.lost_ip_event = 0;
    base_cfg.if_key        = "MESH0";
    base_cfg.if_desc       = "mesh0";
    base_cfg.route_prio    = 10;

    const esp_netif_config_t cfg = {
        .base   = &base_cfg,
        .driver = nullptr,
        .stack  = &s_mesh_netstack,
    };

    _espNetif = esp_netif_new(&cfg);
    if (!_espNetif) return false;

    // Attach driver -- IDF calls post_attach which registers transmit fn
    _driverHandle = new MeshNetifDriverHandle();
    _driverHandle->base.post_attach = mesh_drv_post_attach;
    _driverHandle->base.netif       = nullptr;
    _driverHandle->drv              = this;

    if (esp_netif_attach(_espNetif, _driverHandle) != ESP_OK) {
        esp_netif_destroy(_espNetif);
        _espNetif = nullptr;
        delete _driverHandle;
        _driverHandle = nullptr;
        return false;
    }

    // Cache the underlying lwIP netif — used for direct tcpip_input() calls.
    _lwipNetif = static_cast<struct netif*>(esp_netif_get_netif_impl(_espNetif));

    // Set MAC address (informational)
    esp_netif_set_mac(_espNetif, const_cast<uint8_t*>(mac));

    // Bring the interface up
    esp_netif_action_start(_espNetif, nullptr, 0, nullptr);
    esp_netif_action_connected(_espNetif, nullptr, 0, nullptr);

    // Apply static IP / subnet / gateway
    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr      = static_cast<uint32_t>(localIP);
    ip_info.netmask.addr = static_cast<uint32_t>(subnet);
    ip_info.gw.addr      = static_cast<uint32_t>(gateway);
    esp_netif_set_ip_info(_espNetif, &ip_info);

    return true;
}

void NetifDriver::stop() {
    if (_espNetif) {
        esp_netif_action_disconnected(_espNetif, nullptr, 0, nullptr);
        esp_netif_action_stop(_espNetif, nullptr, 0, nullptr);
        esp_netif_destroy(_espNetif);
        _espNetif  = nullptr;
        _lwipNetif = nullptr;
    }
    if (_driverHandle) {
        delete _driverHandle;
        _driverHandle = nullptr;
    }
}

bool NetifDriver::injectRxPacket(const uint8_t* data, size_t len) {
    if (!_lwipNetif) return false;

    // PBUF_LINK reserves PBUF_LINK_HLEN (14 bytes) of headroom before the
    // payload.  When NAPT forwards a packet from mesh0 to the WiFi STA (an
    // Ethernet netif), ethernet_output() prepends a 14-byte Ethernet header
    // via pbuf_header(-14).  Without this headroom it fails with ERR_BUF and
    // the packet is silently dropped, breaking all NAPT-forwarded traffic.
    struct pbuf* p = pbuf_alloc(PBUF_LINK, static_cast<u16_t>(len), PBUF_POOL);
    if (!p) return false;
    pbuf_take(p, data, static_cast<u16_t>(len));

    err_t err = tcpip_input(p, _lwipNetif);
    if (err != ERR_OK) {
        pbuf_free(p);
        return false;
    }
    return true;
}

void NetifDriver::setDefaultGateway(IPAddress gw) {
    if (!_espNetif) return;

    // Update the gateway in the IP config
    esp_netif_ip_info_t ip_info = {};
    esp_netif_get_ip_info(_espNetif, &ip_info);
    ip_info.gw.addr = static_cast<uint32_t>(gw);
    esp_netif_set_ip_info(_espNetif, &ip_info);

    // Make mesh0 the default netif for unmatched routes
    esp_netif_set_default_netif(_espNetif);

    // Configure the gateway's mesh IP as the primary DNS server in lwIP so that
    // all mesh node DNS queries are directed to the gateway's DNS proxy.
    ip_addr_t gw_addr;
    gw_addr.type = IPADDR_TYPE_V4;
    gw_addr.u_addr.ip4.addr = static_cast<uint32_t>(gw);
    dns_setserver(0, &gw_addr);
}

void NetifDriver::setTxCallback(TxCallback cb, void* ctx) {
    _txCb  = cb;
    _txCtx = ctx;
}

bool NetifDriver::txCallback(const uint8_t* data, size_t len) {
    if (_txCb) return _txCb(data, len, _txCtx);
    return false;
}

#endif // !ESP8266
