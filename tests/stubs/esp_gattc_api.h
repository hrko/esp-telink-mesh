#pragma once
#include <cstdint>
#include <array>
#include <algorithm>
using esp_gatt_if_t = uint8_t;
using esp_gatt_status_t = int;
using esp_bd_addr_t = uint8_t[6];
constexpr esp_gatt_if_t ESP_GATT_IF_NONE = 255;
constexpr int ESP_GATT_OK = 0;
constexpr int ESP_GATT_WRITE_TYPE_RSP = 1;
constexpr int ESP_GATT_WRITE_TYPE_NO_RSP = 2;
constexpr int ESP_GATT_AUTH_REQ_NONE = 0;
inline int write_result = ESP_GATT_OK;
inline unsigned control_writes = 0;
inline std::array<uint8_t, 20> last_control_packet{};
inline int esp_ble_gattc_write_char(esp_gatt_if_t, uint16_t, uint16_t handle, uint16_t length, uint8_t *value, int, int) {
  if (handle == 2 && write_result == ESP_GATT_OK) {
    ++control_writes;
    std::copy_n(value, std::min<size_t>(length, last_control_packet.size()), last_control_packet.begin());
  }
  return write_result;
}
inline int esp_ble_gattc_read_char(esp_gatt_if_t, uint16_t, uint16_t, int) { return ESP_GATT_OK; }
inline int esp_ble_gattc_register_for_notify(esp_gatt_if_t, uint8_t *, uint16_t) { return ESP_GATT_OK; }
enum esp_gattc_cb_event_t {
  ESP_GATTC_DISCONNECT_EVT, ESP_GATTC_CLOSE_EVT, ESP_GATTC_SEARCH_CMPL_EVT,
  ESP_GATTC_WRITE_CHAR_EVT, ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_READ_CHAR_EVT, ESP_GATTC_NOTIFY_EVT
};
struct esp_ble_gattc_cb_param_t {
  struct { uint16_t conn_id, handle; int status; } write;
  struct { uint16_t handle; int status; } reg_for_notify;
  struct { uint16_t conn_id, handle; int status; uint8_t *value; uint16_t value_len; } read;
  struct { uint16_t conn_id, handle; uint8_t *value; uint16_t value_len; } notify;
};
