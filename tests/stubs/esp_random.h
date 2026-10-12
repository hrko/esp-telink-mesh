#pragma once
#include <cstdint>
#include <cstring>
inline uint32_t fake_random = 123;
inline uint32_t esp_random() { return fake_random; }
inline void esp_fill_random(void *data, size_t size) { std::memset(data, 1, size); }
