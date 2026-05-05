cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "../src/MeshNetwork.cpp"
        "../src/PhysicalLayer.cpp"
        "../src/LinkLayer.cpp"
        "../src/Crypto.cpp"
        "../src/PeerManager.cpp"
        "../src/Router.cpp"
        "../src/NetifDriver.cpp"
        "../src/Onboarding.cpp"
        "../src/BatteryNode.cpp"
        "../src/Gateway.cpp"
        "../src/WebUI.cpp"
        "../src/ServiceDiscovery.cpp"
    INCLUDE_DIRS
        "../src"
    REQUIRES
        esp_wifi
        esp_netif
        esp_http_server
        nvs_flash
        mbedtls
        mdns
        lwip
)
