#ifndef MESH_NETIF_DRIVER_H
#define MESH_NETIF_DRIVER_H

#if !defined(ESP8266)

#include <Arduino.h>

struct netif;  // forward declaration (lwIP)

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

    // Called from the lwIP output function
    bool txCallback(const uint8_t* data, size_t len);

private:
    struct netif* _lwipNetif = nullptr;
    TxCallback _txCb = nullptr;
    void* _txCtx = nullptr;
};

#endif // !ESP8266
#endif // MESH_NETIF_DRIVER_H
