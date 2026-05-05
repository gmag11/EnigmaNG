// gateway_hosted - EnigmaNG IDF Native Example
// Gateway with ESP-Hosted dual-board (IDF only)
// Main board: ESP32 running mesh + routing
// Slave board: ESP32 providing WiFi STA via ESP-Hosted protocol

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "MeshNetwork.h"
#include "Gateway.h"

static const char* TAG = "gateway_hosted";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "EnigmaNG Gateway (Hosted/Dual-Board)");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize mesh as gateway
    MeshNetwork mesh;
    if (mesh.begin("MySecretMeshKey123", MESH_GATEWAY)) {
        ESP_LOGI(TAG, "Mesh gateway started");
    } else {
        ESP_LOGE(TAG, "Failed to start mesh gateway");
        return;
    }

    // Start services
    mesh.startWebServer(80);
    mesh.startPrometheus(9090);

    // Main loop
    while (true) {
        mesh.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
