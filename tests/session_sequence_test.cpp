#include <esp_telink_mesh/v1/session_client.h>
#include <esp_random.h>
#include <pair_fixture.h>
#include <cassert>
#include <iostream>
using namespace esp_telink_mesh::v1;

static void pair(SessionClient &session, uint32_t sequence) {
  session.reset();
  session.configure("test", "test", 0x211);
  esp_bd_addr_t address{};
  assert(session.set_link_context(1, 1, address));
  session.set_characteristic_handles(1, 2, 3);
  fake_random = sequence;
  assert(session.begin_pairing());
  assert(session.handle_write_char_evt(3, ESP_GATT_OK, nullptr, nullptr));
  assert(session.handle_read_char_evt(3, ESP_GATT_OK, pair_response.data(), pair_response.size(), nullptr, nullptr));
  assert(session.handle_reg_for_notify_evt(1, ESP_GATT_OK, nullptr, nullptr));
  assert(session.handle_write_char_evt(1, ESP_GATT_OK, nullptr, nullptr));
}

static uint32_t wire_sequence() {
  return last_control_packet[0] | (uint32_t(last_control_packet[1]) << 8) | (uint32_t(last_control_packet[2]) << 16);
}

int main() {
  SessionClient session;
  pair(session, 0xffff);
  assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SENT);
  assert(wire_sequence() == 0xffff);
  assert(session.handle_write_char_evt(2, ESP_GATT_OK, nullptr, nullptr));
  assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SENT);
  assert(wire_sequence() == 0x10000);

  pair(session, 0xffffff);
  assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SENT);
  assert(wire_sequence() == 0xffffff && !session.is_sequence_exhausted());
  assert(session.handle_write_char_evt(2, ESP_GATT_OK, nullptr, nullptr));
  assert(session.is_sequence_exhausted());
  const auto sent = control_writes;
  assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SEQUENCE_EXHAUSTED);
  assert(control_writes == sent);

  pair(session, 0);
  assert(!session.is_sequence_exhausted());
  assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SENT);
  assert(wire_sequence() == 1);
  session.handle_write_char_evt(2, ESP_GATT_OK, nullptr, nullptr);
  write_result = 1;
  assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::WRITE_ERROR);
  write_result = ESP_GATT_OK;
  assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SENT);
  assert(wire_sequence() == 3);
  std::cout << "24-bit sequence boundary tests passed\n";
}
