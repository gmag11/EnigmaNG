#include "NetifDriver.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <cstring>

// Custom driver interface for esp_netif
const esp_netif_driver_ifconfig_t* NetifDriver::_getDriverConfig() {
    static const esp_netif_driver_ifconfig_t s_cfg = {
        .handle = nullptr,
        .transmit = _transmit,
        .transmit_wrap = _transmitWrap,
        .driver_free_rx_buffer = _freeRxBuf,
    };
    return &s_cfg;
}

bool NetifDriver::begin(const uint8_t* mac, IPAddress localIP, IPAddress subnet, IPAddress gateway) {
    // Create custom netif configuration
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    base_cfg.if_key = "MESH0";
    base_cfg.if_desc = "mesh0";
    base_cfg.route_prio = 10;  // Lower priority than wifi_sta

    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = (uint32_t)localIP;
    ip_info.netmask.addr = (uint32_t)subnet;
    ip_info.gw.addr = (uint32_t)gateway;
    base_cfg.ip_info = &ip_info;

    esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = _getDriverConfig(),
        .stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA,
    };

    _netif = esp_netif_new(&cfg);
    if (!_netif) return false;

    // Set MAC address
    esp_netif_set_mac(_netif, (uint8_t*)mac);

    // Attach our custom driver
    esp_netif_attach(_netif, (void*)this);

    // Set static IP (disable DHCP client)
    esp_netif_dhcpc_stop(_netif);
    esp_netif_set_ip_info(_netif, &ip_info);

    // Bring interface up
    esp_netif_action_start(_netif, nullptr, 0, nullptr);

    return true;
}

void NetifDriver::stop() {
    if (_netif) {
        esp_netif_action_stop(_netif, nullptr, 0, nullptr);
        esp_netif_destroy(_netif);
        _netif = nullptr;
    }
}

bool NetifDriver::injectRxPacket(const uint8_t* data, size_t len) {
    if (!_netif) return false;

    // Allocate buffer and copy data
    void* buf = malloc(len);
    if (!buf) return false;
    memcpy(buf, data, len);

    esp_netif_receive(_netif, buf, len, nullptr);
    return true;
}

void NetifDriver::setTxCallback(TxCallback cb, void* ctx) {
    _txCb = cb;
    _txCtx = ctx;
}

esp_err_t NetifDriver::_transmit(void* h, void* buffer, size_t len) {
    NetifDriver* self = (NetifDriver*)h;
    if (self->_txCb) {
        if (self->_txCb((const uint8_t*)buffer, len, self->_txCtx)) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t NetifDriver::_transmitWrap(void* h, void* buffer, size_t len, void* netstack_buf) {
    return _transmit(h, buffer, len);
}

void NetifDriver::_freeRxBuf(void* h, void* buffer) {
    free(buffer);
}

esp_err_t NetifDriver::_postAttach(esp_netif_t* esp_netif, void* args) {
    // Called after driver is attached to netif
    return ESP_OK;
}
