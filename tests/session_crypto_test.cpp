#include <esp_telink_mesh/v1/session_client.h>
#include <aes_alt.h>
#include <pair_fixture.h>
#include <cassert>
#include <iostream>
using namespace esp_telink_mesh::v1;

int main() {
  SessionClient session;
  session.configure("test", "test", 0x211);
  esp_bd_addr_t address{};
  assert(session.set_link_context(1, 1, address));
  session.set_characteristic_handles(1, 2, 3);
  assert(session.begin_pairing());
  assert(session.handle_write_char_evt(3, ESP_GATT_OK, nullptr, nullptr));
  assert(session.handle_read_char_evt(3, ESP_GATT_OK, pair_response.data(), pair_response.size(), nullptr, nullptr));
  assert(session.handle_reg_for_notify_evt(1, ESP_GATT_OK, nullptr, nullptr));
  assert(session.handle_write_char_evt(1, ESP_GATT_OK, nullptr, nullptr));

  // Fixed vectors computed independently with OpenSSL AES-128-ECB.
  const std::array<uint8_t, 20> notify = {
      0x7b, 0x00, 0x00, 0x01, 0x00, 0x96, 0x44, 0x41, 0xd8, 0xcc,
      0x38, 0x25, 0x88, 0x1f, 0xb7, 0xb8, 0x85, 0xdf, 0x8b, 0xb1};
  const std::array<uint8_t, 20> command = {
      0x7b, 0x00, 0x00, 0xba, 0x9d, 0x5a, 0x29, 0x7c, 0x97, 0xc2,
      0xa4, 0x5e, 0xa8, 0x4a, 0xfc, 0x04, 0x14, 0xd5, 0x17, 0x2d};
  DecryptedNotify out{};
  assert(session.handle_notify_packet(1, notify.data(), notify.size(), &out));
  assert(out.src == 1 && out.opcode == 0xdc);
  assert((out.payload == std::array<uint8_t, 10>{1, 1, 50, 3, 0, 0, 0, 0, 0, 0}));
  for (size_t i = 0; i < notify.size(); ++i) {
    auto invalid = notify;
    invalid[i] ^= 1;
    out.opcode = 0x55;
    assert(!session.handle_notify_packet(1, invalid.data(), invalid.size(), &out));
    assert(out.opcode == 0x55);
  }
  const uint8_t payload = 1;
  aes_fail = true;
  assert(!session.handle_notify_packet(1, notify.data(), notify.size(), &out));
  assert(session.send_mesh_command(1, 0xd0, &payload, 1) == SendMeshCommandResult::WRITE_ERROR);
  assert(control_writes == 0);
  aes_fail = false;
  assert(session.send_mesh_command(1, 0xd0, &payload, 1) == SendMeshCommandResult::SENT);
  assert(last_control_packet == command);
  std::cout << "Session crypto vectors and integrity tests passed\n";
}
