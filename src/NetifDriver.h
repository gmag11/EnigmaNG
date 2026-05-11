#ifndef MESH_NETIF_DRIVER_H
#define MESH_NETIF_DRIVER_H

#if !defined(ESP8266)

#include <Arduino.h>
#include "esp_netif.h"

// Internal driver handle — defined in NetifDriver.cpp
struct MeshNetifDriverHandle;
struct netif;

class NetifDriver {
public:
    bool begin(const uint8_t* mac, IPAddress localIP, IPAddress subnet, IPAddress gateway);
    void stop();

    // Inject received IPv4 packet into lwIP stack
    bool injectRxPacket(const uint8_t* data, size_t len);

    // Set the default gateway on the mesh0 netif and make it the default netif.
    // Must be called after a gateway peer is confirmed, so that lwIP can route
    // packets destined for external addresses (outside 10.200.0.0/16).
    void setDefaultGateway(IPAddress gw);

    // TX callback: called by lwIP when it wants to send a packet over mesh
    typedef bool (*TxCallback)(const uint8_t* data, size_t len, void* ctx);
    void setTxCallback(TxCallback cb, void* ctx);

    // Called by the lwIP output function via esp_netif transmit
    bool txCallback(const uint8_t* data, size_t len);

private:
    esp_netif_t*           _espNetif     = nullptr;
    struct netif*          _lwipNetif    = nullptr;  // cached from esp_netif_get_netif_impl()
    MeshNetifDriverHandle* _driverHandle = nullptr;
    TxCallback             _txCb         = nullptr;
    void*                  _txCtx        = nullptr;
};

#endif // !ESP8266
#endif // MESH_NETIF_DRIVER_H
