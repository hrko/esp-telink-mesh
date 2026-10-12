/**
 * @file
 * @brief Implementation of the ESP-IDF Telink Mesh v1 session client.
 */

#include <esp_telink_mesh/v1/session_client.h>

#include <algorithm>
#include <array>
#include <cstring>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#define MBEDTLS_AES_ALT
#include <aes_alt.h>

namespace esp_telink_mesh {
namespace v1 {

namespace {

/// @internal Logging tag.
static const char *const TAG = "esp_telink_mesh";

/**
 * @internal
 * @brief Builds the 16-byte Telink mesh XOR key from credentials.
 */
std::array<uint8_t, 16> mesh_xor_key(const std::string &name, const std::string &password) {
  std::array<uint8_t, 16> out{};
  for (size_t i = 0; i < out.size(); i++) {
    const auto a = static_cast<uint8_t>(i < name.size() ? name[i] : 0);
    const auto b = static_cast<uint8_t>(i < password.size() ? password[i] : 0);
    out[i] = a ^ b;
  }
  return out;
}

/**
 * @internal
 * @brief AES-ECB helper using Telink byte-order convention.
 * @param key AES-128 key material.
 * @param data Plaintext input block.
 * @param out Output ciphertext block.
 * @return `true` on success.
 */
bool aes_encrypt_telink(const std::array<uint8_t, 16> &key, const std::array<uint8_t, 16> &data,
                        std::array<uint8_t, 16> *out) {
  if (out == nullptr) {
    return false;
  }

  uint8_t key_rev[16];
  uint8_t data_rev[16];
  uint8_t enc_rev[16];

  for (size_t i = 0; i < 16; i++) {
    key_rev[i] = key[15 - i];
    data_rev[i] = data[15 - i];
  }

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  const auto key_status = mbedtls_aes_setkey_enc(&ctx, key_rev, 128);
  if (key_status != 0) {
    mbedtls_aes_free(&ctx);
    return false;
  }
  const auto enc_status = mbedtls_aes_crypt_ecb(&ctx, ESP_AES_ENCRYPT, data_rev, enc_rev);
  mbedtls_aes_free(&ctx);
  if (enc_status != 0) {
    return false;
  }

  for (size_t i = 0; i < 16; i++) {
    (*out)[i] = enc_rev[15 - i];
  }
  return true;
}

/**
 * @internal
 * @brief Derives encrypted pairing bytes from a random challenge block.
 */
bool key_encrypt(const std::string &mesh_name, const std::string &mesh_password, const std::array<uint8_t, 16> &key,
                 std::array<uint8_t, 16> *out) {
  return aes_encrypt_telink(key, mesh_xor_key(mesh_name, mesh_password), out);
}

/**
 * @internal
 * @brief Derives the session key from local random bytes and peer response bytes.
 */
bool generate_session_key(const std::string &mesh_name, const std::string &mesh_password,
                          const std::array<uint8_t, 8> &data1, const uint8_t *data2_8,
                          std::array<uint8_t, 16> *out) {
  if (data2_8 == nullptr || out == nullptr) {
    return false;
  }

  std::array<uint8_t, 16> data{};
  for (size_t i = 0; i < 8; i++) {
    data[i] = data1[i];
    data[8 + i] = data2_8[i];
  }
  return aes_encrypt_telink(mesh_xor_key(mesh_name, mesh_password), data, out);
}

/**
 * @internal
 * @brief Writes a boolean value only when the destination pointer is non-null.
 */
void set_flag_(bool *out, bool value) {
  if (out != nullptr) {
    *out = value;
  }
}

}  // namespace

void SessionClient::configure(const std::string &mesh_name, const std::string &mesh_password, uint16_t vendor_id) {
  this->mesh_name_ = mesh_name;
  this->mesh_password_ = mesh_password;
  this->vendor_id_ = vendor_id;
}

void SessionClient::reset() {
  this->gattc_if_ = ESP_GATT_IF_NONE;
  this->conn_id_ = 0;
  memset(this->remote_bda_, 0, sizeof(this->remote_bda_));

  this->notify_handle_ = 0;
  this->control_handle_ = 0;
  this->pair_handle_ = 0;

  this->mac_data_.fill(0);
  this->pair_random_.fill(0);
  this->session_key_.fill(0);

  this->setup_step_ = SetupStep::IDLE;
  this->setup_step_started_us_ = 0;
  this->control_write_pending_ = false;
  this->control_write_started_us_ = 0;

  this->packet_count_ = 1;
}

bool SessionClient::set_link_context(esp_gatt_if_t gattc_if, uint16_t conn_id, const esp_bd_addr_t remote_bda) {
  if (gattc_if == ESP_GATT_IF_NONE) {
    return false;
  }
  this->gattc_if_ = gattc_if;
  this->conn_id_ = conn_id;
  memcpy(this->remote_bda_, remote_bda, sizeof(this->remote_bda_));
  this->set_mac_data_from_bda_(remote_bda);
  return true;
}

void SessionClient::set_characteristic_handles(uint16_t notify_handle, uint16_t control_handle, uint16_t pair_handle) {
  this->notify_handle_ = notify_handle;
  this->control_handle_ = control_handle;
  this->pair_handle_ = pair_handle;
}

void SessionClient::set_mac_data_from_bda_(const esp_bd_addr_t remote_bda) {
  this->mac_data_[0] = remote_bda[5];
  this->mac_data_[1] = remote_bda[4];
  this->mac_data_[2] = remote_bda[3];
  this->mac_data_[3] = remote_bda[2];
  this->mac_data_[4] = remote_bda[1];
  this->mac_data_[5] = remote_bda[0];
}

bool SessionClient::begin_pairing() {
  if (this->pair_handle_ == 0 || this->notify_handle_ == 0 || this->control_handle_ == 0 ||
      this->gattc_if_ == ESP_GATT_IF_NONE || this->setup_step_ != SetupStep::IDLE) {
    ESP_LOGW(TAG, "pairing prerequisites are not ready");
    return false;
  }

  std::array<uint8_t, 16> pair_data{};
  esp_fill_random(this->pair_random_.data(), this->pair_random_.size());
  for (size_t i = 0; i < this->pair_random_.size(); i++) {
    pair_data[i] = this->pair_random_[i];
  }

  std::array<uint8_t, 16> enc_data{};
  if (!key_encrypt(this->mesh_name_, this->mesh_password_, pair_data, &enc_data)) {
    ESP_LOGE(TAG, "key_encrypt failed");
    return false;
  }

  uint8_t packet[17] = {0};
  packet[0] = 0x0C;
  for (size_t i = 0; i < 8; i++) {
    packet[1 + i] = pair_data[i];
    packet[9 + i] = enc_data[i];
  }

  const auto status = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->pair_handle_, sizeof(packet),
                                                packet, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_GATT_OK) {
    ESP_LOGW(TAG, "pair write failed, status=%d", status);
    return false;
  }

  this->set_setup_step_(SetupStep::PAIR_WRITE);
  return true;
}

bool SessionClient::handle_write_char_evt(uint16_t handle, esp_gatt_status_t status, bool *need_disconnect,
                                          bool *session_established) {
  set_flag_(need_disconnect, false);
  set_flag_(session_established, false);

  if (this->control_write_pending_ && handle == this->control_handle_) {
    this->control_write_pending_ = false;
    if (status != ESP_GATT_OK) {
      ESP_LOGW(TAG, "control write failed handle=0x%04X status=%d", handle, status);
    } else {
      ESP_LOGV(TAG, "control write ack handle=0x%04X", handle);
    }
    return status == ESP_GATT_OK;
  }

  if (this->setup_step_ == SetupStep::PAIR_WRITE && handle == this->pair_handle_) {
    if (status != ESP_GATT_OK) {
      return this->fail_setup_(need_disconnect);
    }
    const auto read_status =
        esp_ble_gattc_read_char(this->gattc_if_, this->conn_id_, this->pair_handle_, ESP_GATT_AUTH_REQ_NONE);
    if (read_status != ESP_GATT_OK) {
      ESP_LOGW(TAG, "pair read failed status=%d", read_status);
      return this->fail_setup_(need_disconnect);
    }
    this->set_setup_step_(SetupStep::PAIR_READ);
    return true;
  }

  if (this->setup_step_ == SetupStep::NOTIFY_ENABLE && handle == this->notify_handle_) {
    if (status != ESP_GATT_OK) {
      return this->fail_setup_(need_disconnect);
    }
    this->set_setup_step_(SetupStep::READY);
    set_flag_(session_established, true);
    return true;
  }

  return false;
}

bool SessionClient::handle_reg_for_notify_evt(uint16_t handle, esp_gatt_status_t status, bool *need_disconnect,
                                              bool *session_established) {
  set_flag_(need_disconnect, false);
  set_flag_(session_established, false);

  if (this->setup_step_ != SetupStep::NOTIFY_REGISTER || handle != this->notify_handle_) {
    return false;
  }

  if (status != ESP_GATT_OK) {
    ESP_LOGW(TAG, "REG_FOR_NOTIFY failed status=%d", status);
    return this->fail_setup_(need_disconnect);
  }
  if (!this->write_notify_enable_(session_established)) {
    return this->fail_setup_(need_disconnect);
  }
  return true;
}

bool SessionClient::handle_read_char_evt(uint16_t handle, esp_gatt_status_t status, const uint8_t *value,
                                         uint16_t value_len, bool *need_disconnect, bool *session_established) {
  set_flag_(need_disconnect, false);
  set_flag_(session_established, false);

  if (this->setup_step_ != SetupStep::PAIR_READ || handle != this->pair_handle_) {
    return false;
  }

  if (status != ESP_GATT_OK) {
    ESP_LOGW(TAG, "pair read status=%d", status);
    return this->fail_setup_(need_disconnect);
  }

  if (!this->handle_pairing_response_(value, value_len, need_disconnect, session_established)) {
    return this->fail_setup_(need_disconnect);
  }
  return true;
}

bool SessionClient::handle_pairing_response_(const uint8_t *data, uint16_t len, bool *need_disconnect,
                                             bool *session_established) {
  if (data == nullptr || len < 17 || data[0] != 0x0D) {
    ESP_LOGW(TAG, "invalid pairing response len=%u", len);
    set_flag_(need_disconnect, true);
    return false;
  }

  std::array<uint8_t, 16> peer_random{};
  std::copy_n(data + 1, 8, peer_random.begin());
  std::array<uint8_t, 16> expected{};
  if (!key_encrypt(this->mesh_name_, this->mesh_password_, peer_random, &expected)) {
    set_flag_(need_disconnect, true);
    return false;
  }
  uint8_t difference = 0;
  for (size_t i = 0; i < 8; ++i) {
    difference |= expected[i] ^ data[9 + i];
  }
  if (difference != 0) {
    ESP_LOGW(TAG, "pairing response authentication failed");
    set_flag_(need_disconnect, true);
    return false;
  }

  if (!generate_session_key(this->mesh_name_, this->mesh_password_, this->pair_random_, data + 1, &this->session_key_)) {
    ESP_LOGW(TAG, "session key generation failed");
    set_flag_(need_disconnect, true);
    return false;
  }

  this->packet_count_ = esp_random() & 0xFFFFFF;
  if (this->packet_count_ == 0) {
    this->packet_count_ = 1;
  }

  (void) session_established;
  return this->register_notify_();
}

bool SessionClient::register_notify_() {
  if (this->notify_handle_ == 0 || this->gattc_if_ == ESP_GATT_IF_NONE) {
    return false;
  }

  const auto status = esp_ble_gattc_register_for_notify(this->gattc_if_, this->remote_bda_, this->notify_handle_);
  if (status != ESP_GATT_OK) {
    ESP_LOGW(TAG, "register_for_notify failed status=%d", status);
    return false;
  }

  this->set_setup_step_(SetupStep::NOTIFY_REGISTER);
  return true;
}

bool SessionClient::write_notify_enable_(bool *session_established) {
  if (this->notify_handle_ == 0 || this->gattc_if_ == ESP_GATT_IF_NONE) {
    return false;
  }

  uint8_t value = 0x01;
  auto status = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->notify_handle_, sizeof(value), &value,
                                         ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status == ESP_GATT_OK) {
    this->set_setup_step_(SetupStep::NOTIFY_ENABLE);
    return true;
  }

  status = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->notify_handle_, sizeof(value), &value,
                                    ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_GATT_OK) {
    ESP_LOGW(TAG, "notify enable write failed, status=%d", status);
    return false;
  }

  this->set_setup_step_(SetupStep::READY);
  set_flag_(session_established, true);
  return true;
}

bool SessionClient::encrypt_packet_(uint8_t *packet20) const {
  std::array<uint8_t, 16> auth_nonce{};
  auth_nonce[0] = this->mac_data_[0];
  auth_nonce[1] = this->mac_data_[1];
  auth_nonce[2] = this->mac_data_[2];
  auth_nonce[3] = this->mac_data_[3];
  auth_nonce[4] = 0x01;
  auth_nonce[5] = packet20[0];
  auth_nonce[6] = packet20[1];
  auth_nonce[7] = packet20[2];
  auth_nonce[8] = 15;

  std::array<uint8_t, 16> authenticator{};
  if (!aes_encrypt_telink(this->session_key_, auth_nonce, &authenticator)) {
    return false;
  }
  for (size_t i = 0; i < 15; i++) {
    authenticator[i] ^= packet20[i + 5];
  }

  std::array<uint8_t, 16> mic{};
  if (!aes_encrypt_telink(this->session_key_, authenticator, &mic)) {
    return false;
  }
  packet20[3] = mic[0];
  packet20[4] = mic[1];

  std::array<uint8_t, 16> iv{};
  iv[1] = this->mac_data_[0];
  iv[2] = this->mac_data_[1];
  iv[3] = this->mac_data_[2];
  iv[4] = this->mac_data_[3];
  iv[5] = 0x01;
  iv[6] = packet20[0];
  iv[7] = packet20[1];
  iv[8] = packet20[2];

  std::array<uint8_t, 16> stream{};
  if (!aes_encrypt_telink(this->session_key_, iv, &stream)) {
    return false;
  }
  for (size_t i = 0; i < 15; i++) {
    packet20[i + 5] ^= stream[i];
  }
  return true;
}

bool SessionClient::decrypt_packet_(uint8_t *packet20) const {
  std::array<uint8_t, 16> iv{};
  iv[0] = this->mac_data_[0];
  iv[1] = this->mac_data_[1];
  iv[2] = this->mac_data_[2];
  iv[3] = packet20[0];
  iv[4] = packet20[1];
  iv[5] = packet20[2];
  iv[6] = packet20[3];
  iv[7] = packet20[4];

  std::array<uint8_t, 16> plaintext{};
  plaintext[0] = 0;
  for (size_t i = 0; i < 15; i++) {
    plaintext[i + 1] = iv[i];
  }

  std::array<uint8_t, 16> stream{};
  if (!aes_encrypt_telink(this->session_key_, plaintext, &stream)) {
    return false;
  }
  for (size_t i = 0; i + 7 < 20; i++) {
    packet20[i + 7] ^= stream[i];
  }

  iv[8] = 13;
  std::array<uint8_t, 16> authenticator{};
  if (!aes_encrypt_telink(this->session_key_, iv, &authenticator)) {
    return false;
  }
  for (size_t i = 0; i < 13; ++i) {
    authenticator[i] ^= packet20[7 + i];
  }
  std::array<uint8_t, 16> mic{};
  if (!aes_encrypt_telink(this->session_key_, authenticator, &mic)) {
    return false;
  }
  return ((mic[0] ^ packet20[5]) | (mic[1] ^ packet20[6])) == 0;
}

bool SessionClient::handle_notify_packet(uint16_t handle, const uint8_t *value, uint16_t value_len,
                                         DecryptedNotify *out) const {
  if (!this->is_session_ready() || handle != this->notify_handle_ || value == nullptr || value_len != 20 || out == nullptr) {
    return false;
  }

  uint8_t packet[20] = {0};
  memcpy(packet, value, sizeof(packet));
  if (!this->decrypt_packet_(packet)) {
    return false;
  }

  out->opcode = packet[7];
  out->src = packet[3] | (static_cast<uint16_t>(packet[4]) << 8);
  for (size_t i = 0; i < out->payload.size(); i++) {
    out->payload[i] = packet[10 + i];
  }
  return true;
}

SendMeshCommandResult SessionClient::send_mesh_command(uint16_t target, uint8_t opcode, const uint8_t *payload,
                                                       size_t payload_len) {
  if (!this->is_session_ready()) {
    return SendMeshCommandResult::NOT_READY;
  }
  if (this->control_handle_ == 0 || this->gattc_if_ == ESP_GATT_IF_NONE) {
    return SendMeshCommandResult::NOT_READY;
  }
  if (this->control_write_pending_) {
    return SendMeshCommandResult::BUSY_PENDING_ACK;
  }
  if (this->is_sequence_exhausted()) {
    return SendMeshCommandResult::SEQUENCE_EXHAUSTED;
  }

  uint8_t packet[20] = {0};
  packet[0] = this->packet_count_ & 0xFF;
  packet[1] = (this->packet_count_ >> 8) & 0xFF;
  packet[2] = (this->packet_count_ >> 16) & 0xFF;
  packet[5] = target & 0xFF;
  packet[6] = (target >> 8) & 0xFF;
  packet[7] = opcode;
  packet[8] = this->vendor_id_ & 0xFF;
  packet[9] = (this->vendor_id_ >> 8) & 0xFF;

  const auto data_len = std::min(payload_len, static_cast<size_t>(10));
  for (size_t i = 0; i < data_len; i++) {
    packet[10 + i] = payload[i];
  }

  if (!this->encrypt_packet_(packet)) {
    return SendMeshCommandResult::WRITE_ERROR;
  }

  // Reserve the nonce even if the transport rejects the write.
  const uint32_t sequence = this->packet_count_++;
  const auto status =
      esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->control_handle_, sizeof(packet), packet,
                               ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_GATT_OK) {
    ESP_LOGW(TAG, "send_packet write failed status=%d", status);
    return SendMeshCommandResult::WRITE_ERROR;
  }

  this->control_write_pending_ = true;
  this->control_write_started_us_ = esp_timer_get_time();
  ESP_LOGV(TAG, "tx seq=0x%06X target=0x%04X opcode=0x%02X len=%u", static_cast<unsigned>(sequence), target, opcode,
           static_cast<unsigned>(data_len));
  (void) sequence;

  return SendMeshCommandResult::SENT;
}

bool SessionClient::is_control_write_timed_out(uint32_t timeout_ms) const {
  return this->control_write_pending_ &&
         esp_timer_get_time() - this->control_write_started_us_ >= static_cast<int64_t>(timeout_ms) * 1000;
}

void SessionClient::set_setup_step_(SetupStep step) {
  this->setup_step_ = step;
  this->setup_step_started_us_ = esp_timer_get_time();
}

bool SessionClient::fail_setup_(bool *need_disconnect) {
  this->reset();
  set_flag_(need_disconnect, true);
  return false;
}

bool SessionClient::is_setup_timed_out(uint32_t timeout_ms) const {
  return this->setup_step_ != SetupStep::IDLE && this->setup_step_ != SetupStep::READY &&
         esp_timer_get_time() - this->setup_step_started_us_ >= static_cast<int64_t>(timeout_ms) * 1000;
}

}  // namespace v1
}  // namespace esp_telink_mesh
