#pragma once
// esp_netif.h stub for native unit test builds.
#include "esp_wifi.h"   // for esp_err_t / ESP_OK
#include "IPAddress.h"
#include <stdint.h>
#include <string.h>

typedef struct esp_netif_obj esp_netif_t;

typedef struct { uint32_t addr; } esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

typedef struct {
    const char* if_key;
    const char* if_desc;
    int         route_prio;
    const esp_netif_ip_info_t* ip_info;
} esp_netif_inherent_config_t;

typedef esp_err_t (*esp_netif_transmit_t)(void*, void*, size_t);
typedef esp_err_t (*esp_netif_transmit_wrap_t)(void*, void*, size_t, void*);
typedef void      (*esp_netif_free_rx_buffer_t)(void*, void*);

typedef struct {
    void*                       handle;
    esp_netif_transmit_t        transmit;
    esp_netif_transmit_wrap_t   transmit_wrap;
    esp_netif_free_rx_buffer_t  driver_free_rx_buffer;
} esp_netif_driver_ifconfig_t;

typedef void* esp_netif_netstack_config_t;
#define ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA ((esp_netif_netstack_config_t*)nullptr)

typedef struct {
    const esp_netif_inherent_config_t*  base;
    const esp_netif_driver_ifconfig_t*  driver;
    esp_netif_netstack_config_t*        stack;
} esp_netif_config_t;

#define ESP_NETIF_INHERENT_DEFAULT_WIFI_STA() esp_netif_inherent_config_t{}

inline esp_netif_t* esp_netif_new(const esp_netif_config_t*)          { return nullptr; }
inline esp_netif_t* esp_netif_get_handle_from_ifkey(const char*)      { return nullptr; }
inline esp_err_t esp_netif_set_mac(esp_netif_t*, uint8_t*)             { return ESP_OK; }
inline esp_err_t esp_netif_attach(esp_netif_t*, void*)                 { return ESP_OK; }
inline esp_err_t esp_netif_dhcpc_stop(esp_netif_t*)                    { return ESP_OK; }
inline esp_err_t esp_netif_set_ip_info(esp_netif_t*, const esp_netif_ip_info_t*) { return ESP_OK; }
inline esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t* info) { if (info) memset(info, 0, sizeof(*info)); return ESP_OK; }
inline void      esp_netif_set_default_netif(esp_netif_t*)             {}
inline void      esp_netif_action_start(esp_netif_t*, void*, int, void*) {}
inline void      esp_netif_action_stop(esp_netif_t*, void*, int, void*)  {}
inline void      esp_netif_destroy(esp_netif_t*)                        {}
inline esp_err_t esp_netif_receive(esp_netif_t*, void*, size_t, void*) { return ESP_OK; }

// DNS info stub — configurable for unit tests
typedef enum {
    ESP_NETIF_DNS_MAIN = 0,
    ESP_NETIF_DNS_BACKUP,
    ESP_NETIF_DNS_FALLBACK,
} esp_netif_dns_type_t;

typedef struct {
    esp_ip4_addr_t ip;
} esp_netif_dns_info_t;

// Test-configurable upstream DNS IP (default: 8.8.8.8)
inline esp_netif_dns_info_t& _stub_dns_info() {
    static esp_netif_dns_info_t info = { .ip = { .addr = 0x08080808u } };
    return info;
}

inline esp_err_t esp_netif_get_dns_info(esp_netif_t*, esp_netif_dns_type_t,
                                         esp_netif_dns_info_t* dns) {
    if (dns) *dns = _stub_dns_info();
    return ESP_OK;
}
