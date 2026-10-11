#include <esp_telink_mesh/v1/session_client.h>
#include <esp_timer.h>
#include <cassert>
#include <iostream>
using namespace esp_telink_mesh::v1;

static void pair(SessionClient &session) {
  session.reset();
  session.configure("test", "test", 0x211);
  esp_bd_addr_t address{};
  assert(session.set_link_context(1, 1, address));
  session.set_characteristic_handles(1, 2, 3);
  assert(session.begin_pairing());
  bool disconnect = false, established = false;
  assert(session.handle_write_char_evt(3, ESP_GATT_OK, &disconnect, &established));
  uint8_t response[9]{};
  assert(session.handle_read_char_evt(3, ESP_GATT_OK, response, 9, &disconnect, &established));
  assert(session.handle_reg_for_notify_evt(1, ESP_GATT_OK, &disconnect, &established));
  assert(session.handle_write_char_evt(1, ESP_GATT_OK, &disconnect, &established));
  assert(established && !disconnect);
}

int main() {
  for (int64_t start_ms : {0LL, 2160000000LL, 4294967000LL}) {
    SessionClient session;
    fake_time_us = start_ms * 1000;
    pair(session);
    assert(!session.is_control_write_timed_out(10000));
    assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SENT);
    fake_time_us += 9999000;
    assert(!session.is_control_write_timed_out(10000));
    assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::BUSY_PENDING_ACK);
    // An unrelated completion must not release the pending write.
    session.handle_write_char_evt(1, ESP_GATT_OK, nullptr, nullptr);
    fake_time_us += 1000;
    assert(session.is_control_write_timed_out(10000));
    session.reset();
    assert(!session.is_control_write_timed_out(10000));
    assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::NOT_READY);
    pair(session);
    assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::SENT);
    session.handle_write_char_evt(2, ESP_GATT_OK, nullptr, nullptr);
    fake_time_us += 20000000;
    assert(!session.is_control_write_timed_out(10000));
    write_result = 1;
    assert(session.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::WRITE_ERROR);
    assert(!session.is_control_write_timed_out(10000));
    write_result = ESP_GATT_OK;
  }
  std::cout << "Session timeout tests passed\n";
}
