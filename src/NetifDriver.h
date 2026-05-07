#ifndef MESH_NETIF_DRIVER_H
#define MESH_NETIF_DRIVER_H

#include <Arduino.h>

struct netif;  // forward declaration (lwIP)

class NetifDriver {
public:
    bool begin(const uint8_t* mac, IPAddress localIP, IPAddress subnet, IPAddress gateway);
    void stop();

    // Inject received IPv4 packet into lwIP stack
    bool injectRxPacket(const uint8_t* data, size_t len);

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

#endif // MESH_NETIF_DRIVER_H
