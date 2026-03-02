/**
 * @file
 * @brief ESP-IDF Telink Mesh v1 session client public API.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_gattc_api.h>

namespace esp_telink_mesh {
namespace v1 {

/**
 * @brief Decrypted mesh notify frame fields.
 */
struct DecryptedNotify {
  /// Mesh opcode (`packet[7]` in decrypted 20-byte frame).
  uint8_t opcode{0};
  /// Source mesh address (`packet[3..4]`, little-endian).
  uint16_t src{0};
  /// 10-byte application payload (`packet[10..19]`).
  std::array<uint8_t, 10> payload{};
};

/**
 * @brief Result code for `SessionClient::send_mesh_command`.
 */
enum class SendMeshCommandResult : uint8_t {
  /// Packet was queued to GATT and awaits write response.
  SENT = 0,
  /// A previous control write is still pending acknowledgment.
  BUSY_PENDING_ACK = 1,
  /// Session or control link context is not ready.
  NOT_READY = 2,
  /// GATT write call failed immediately.
  WRITE_ERROR = 3,
};

/**
 * @brief Stateful Telink Mesh v1 pairing/session transport client for ESP-IDF.
 * @note This class is intended for single-threaded, event-loop usage.
 */
class SessionClient {
 public:
  /**
   * @brief Sets mesh credentials and vendor id used for pairing and command packets.
   * @param mesh_name Mesh name string from the mobile app export.
   * @param mesh_password Mesh password string from the mobile app export.
   * @param vendor_id Vendor identifier written into command frames.
   */
  void configure(const std::string &mesh_name, const std::string &mesh_password, uint16_t vendor_id);

  /**
   * @brief Resets link/session runtime state.
   * @note Credential fields and `vendor_id` remain configured.
   */
  void reset();

  /**
   * @brief Applies the active GATT link context and peer address.
   * @param gattc_if GATT client interface from ESP-IDF events.
   * @param conn_id Active connection id.
   * @param remote_bda Remote BLE address.
   * @return `true` when accepted, `false` if required values are invalid.
   */
  bool set_link_context(esp_gatt_if_t gattc_if, uint16_t conn_id, const esp_bd_addr_t remote_bda);

  /**
   * @brief Sets characteristic handles used by pairing/control/notify flows.
   * @param notify_handle Notify characteristic handle.
   * @param control_handle Control write characteristic handle.
   * @param pair_handle Pairing characteristic handle.
   */
  void set_characteristic_handles(uint16_t notify_handle, uint16_t control_handle, uint16_t pair_handle);

  /**
   * @brief Starts the Telink pairing handshake by writing a 17-byte challenge packet.
   * @return `true` when the write request is accepted.
   */
  bool begin_pairing();

  /**
   * @brief Handles ESP_GATTC_WRITE_CHAR_EVT for pairing/control/notify-enable writes.
   * @param handle Characteristic handle from the event.
   * @param status GATT status from the event.
   * @param need_disconnect Optional flag set when the caller should disconnect.
   * @param session_established Optional flag set when the session is ready to use.
   * @return `true` when this event was handled successfully.
   */
  bool handle_write_char_evt(uint16_t handle, esp_gatt_status_t status, bool *need_disconnect,
                             bool *session_established);

  /**
   * @brief Handles ESP_GATTC_REG_FOR_NOTIFY_EVT for the notify characteristic.
   * @param handle Characteristic handle from the event.
   * @param status GATT status from the event.
   * @param need_disconnect Optional flag set when the caller should disconnect.
   * @param session_established Optional flag set when notify is enabled and session is usable.
   * @return `true` when this event was handled successfully.
   */
  bool handle_reg_for_notify_evt(uint16_t handle, esp_gatt_status_t status, bool *need_disconnect,
                                  bool *session_established);

  /**
   * @brief Handles ESP_GATTC_READ_CHAR_EVT for pairing-response data.
   * @param handle Characteristic handle from the event.
   * @param status GATT status from the event.
   * @param value Read payload bytes.
   * @param value_len Number of bytes in `value`.
   * @param need_disconnect Optional flag set when the caller should disconnect.
   * @param session_established Optional flag set when notify has been enabled immediately.
   * @return `true` when this event was handled successfully.
   */
  bool handle_read_char_evt(uint16_t handle, esp_gatt_status_t status, const uint8_t *value, uint16_t value_len,
                            bool *need_disconnect, bool *session_established);

  /**
   * @brief Decrypts and decodes a 20-byte mesh notify packet.
   * @param handle Characteristic handle that emitted the notify event.
   * @param value Raw notify bytes.
   * @param value_len Number of bytes in `value` (must be 20).
   * @param out Output decoded fields.
   * @return `true` when packet was accepted and decoded.
   */
  bool handle_notify_packet(uint16_t handle, const uint8_t *value, uint16_t value_len, DecryptedNotify *out) const;

  /**
   * @brief Encrypts and sends a mesh command frame over the control characteristic.
   * @param target Mesh destination address.
   * @param opcode Mesh opcode.
   * @param payload Command payload bytes.
   * @param payload_len Number of bytes available at `payload`.
   * @return Send result code.
   * @note Payload is truncated to 10 bytes to fit the Telink frame layout.
   */
  SendMeshCommandResult send_mesh_command(uint16_t target, uint8_t opcode, const uint8_t *payload, size_t payload_len);

  /**
   * @brief Indicates whether a valid session key has been established.
   */
  bool is_session_ready() const { return this->session_ready_; }

  /**
   * @brief Gets the configured notify characteristic handle.
   */
  uint16_t notify_handle() const { return this->notify_handle_; }

  /**
   * @brief Gets the configured control characteristic handle.
   */
  uint16_t control_handle() const { return this->control_handle_; }

  /**
   * @brief Gets the configured pairing characteristic handle.
   */
  uint16_t pair_handle() const { return this->pair_handle_; }

 private:
  /**
   * @internal
   * @brief Registers for notify on the configured notify characteristic.
   */
  bool register_notify_();

  /**
   * @internal
   * @brief Enables notify by writing `0x01` to the notify characteristic.
   * @param session_established Optional flag set true for no-response fallback writes.
   */
  bool write_notify_enable_(bool *session_established);

  /**
   * @internal
   * @brief Processes raw pairing response bytes and derives session state.
   * @param data Pairing response bytes.
   * @param len Number of bytes in `data`.
   * @param need_disconnect Optional flag set when caller should disconnect.
   * @param session_established Optional flag set when session becomes usable.
   */
  bool handle_pairing_response_(const uint8_t *data, uint16_t len, bool *need_disconnect, bool *session_established);

  /**
   * @internal
   * @brief Encrypts a 20-byte outgoing mesh packet in-place.
   * @param packet20 Telink frame buffer.
   */
  void encrypt_packet_(uint8_t *packet20) const;

  /**
   * @internal
   * @brief Decrypts a 20-byte incoming mesh packet in-place.
   * @param packet20 Telink frame buffer.
   */
  void decrypt_packet_(uint8_t *packet20) const;

  /**
   * @internal
   * @brief Stores MAC bytes in Telink protocol order from ESP-IDF address order.
   * @param remote_bda Remote BLE address.
   */
  void set_mac_data_from_bda_(const esp_bd_addr_t remote_bda);

  std::string mesh_name_;
  std::string mesh_password_;

  uint16_t vendor_id_{0x0211};

  esp_gatt_if_t gattc_if_{ESP_GATT_IF_NONE};
  uint16_t conn_id_{0};
  esp_bd_addr_t remote_bda_{};

  uint16_t notify_handle_{0};
  uint16_t control_handle_{0};
  uint16_t pair_handle_{0};

  std::array<uint8_t, 6> mac_data_{};
  std::array<uint8_t, 8> pair_random_{};
  std::array<uint8_t, 16> session_key_{};

  bool pairing_write_pending_{false};
  bool pairing_read_pending_{false};
  bool notify_register_pending_{false};
  bool notify_enable_pending_{false};
  bool control_write_pending_{false};
  bool session_ready_{false};

  uint16_t packet_count_{1};
};

}  // namespace v1
}  // namespace esp_telink_mesh
