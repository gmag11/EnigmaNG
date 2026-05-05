#ifndef MESH_NETIF_DRIVER_H
#define MESH_NETIF_DRIVER_H

#include <Arduino.h>
#include "esp_netif.h"

class NetifDriver {
public:
    bool begin(const uint8_t* mac, IPAddress localIP, IPAddress subnet, IPAddress gateway);
    void stop();

    // Inject received IPv4 packet into lwIP stack
    bool injectRxPacket(const uint8_t* data, size_t len);

    // Get the esp_netif handle
    esp_netif_t* getNetif() { return _netif; }

    // TX callback: called by lwIP when it wants to send a packet over mesh
    typedef bool (*TxCallback)(const uint8_t* data, size_t len, void* ctx);
    void setTxCallback(TxCallback cb, void* ctx);

private:
    esp_netif_t* _netif = nullptr;
    TxCallback _txCb = nullptr;
    void* _txCtx = nullptr;

    static esp_err_t _transmit(void* h, void* buffer, size_t len);
    static esp_err_t _transmitWrap(void* h, void* buffer, size_t len, void* netstack_buf);
    static void _freeRxBuf(void* h, void* buffer);
    static esp_err_t _postAttach(esp_netif_t* esp_netif, void* args);
    static const esp_netif_driver_ifconfig_t* _getDriverConfig();
};

#endif // MESH_NETIF_DRIVER_H
