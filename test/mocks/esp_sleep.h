#pragma once
// esp_sleep.h stub for native unit test builds.
#include <stdint.h>

typedef uint32_t esp_sleep_wakeup_cause_t;
#define ESP_SLEEP_WAKEUP_UNDEFINED  0
#define ESP_SLEEP_WAKEUP_TIMER      5

inline void esp_deep_sleep_start()              {}
inline void esp_light_sleep_start()             {}
inline int  esp_sleep_enable_timer_wakeup(uint64_t) { return 0; }
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
