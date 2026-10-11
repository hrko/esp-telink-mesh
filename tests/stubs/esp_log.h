#pragma once
template<typename... Args> inline void test_log(Args...) {}
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGV(...) test_log(__VA_ARGS__)
