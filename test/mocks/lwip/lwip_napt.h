#pragma once
// lwip/lwip_napt.h stub for native unit test builds.
#include <stdint.h>
inline int ip_napt_enable(uint32_t, int) { return 0; }
