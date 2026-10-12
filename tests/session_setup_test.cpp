#include <esp_telink_mesh/v1/session_client.h>
#include <esp_timer.h>
#include <pair_fixture.h>
#include <cassert>
#include <iostream>
using namespace esp_telink_mesh::v1;

static bool advance(SessionClient &s, unsigned step, int status, bool *disconnect, bool *established) {
  if (step == 0) return s.handle_write_char_evt(3, status, disconnect, established);
  if (step == 1) return s.handle_read_char_evt(3, status, pair_response.data(), pair_response.size(), disconnect, established);
  if (step == 2) return s.handle_reg_for_notify_evt(1, status, disconnect, established);
  return s.handle_write_char_evt(1, status, disconnect, established);
}

static void start(SessionClient &s, unsigned step) {
  s.reset();
  write_result = read_result = register_result = notify_rsp_result = notify_no_rsp_result = ESP_GATT_OK;
  fake_time_us = 0;
  s.configure("test", "test", 0x211);
  esp_bd_addr_t address{};
  assert(s.set_link_context(1, 1, address));
  s.set_characteristic_handles(1, 2, 3);
  assert(s.begin_pairing());
  for (unsigned i = 0; i < step; ++i) {
    fake_time_us += 9000000;
    bool established = true;
    assert(advance(s, i, ESP_GATT_OK, nullptr, &established));
    assert(!established && !s.is_session_ready());
  }
}

int main() {
  SessionClient s;
  for (unsigned step = 0; step < 4; ++step) {
    start(s, step);
    assert(!s.begin_pairing());
    assert(s.send_mesh_command(1, 0xd0, nullptr, 0) == SendMeshCommandResult::NOT_READY);
    fake_time_us += 9999999;
    assert(!s.is_setup_timed_out(10000));
    bool disconnect = true;
    bool established = true;
    assert(!s.handle_write_char_evt(99, 1, &disconnect, &established));
    assert(!disconnect && !established);
    ++fake_time_us;
    assert(s.is_setup_timed_out(10000));
    s.reset();
    assert(!s.is_setup_timed_out(10000));
    assert(!advance(s, step, ESP_GATT_OK, &disconnect, &established));
    assert(!s.is_session_ready() && !established);

    start(s, step);
    assert(!advance(s, step, 1, &disconnect, &established));
    assert(disconnect && !established && !s.is_session_ready());
    assert(!s.is_setup_timed_out(0));
    assert(!advance(s, step, ESP_GATT_OK, &disconnect, &established));
  }
  for (unsigned step = 0; step < 3; ++step) {
    start(s, step);
    if (step == 0) read_result = 1;
    if (step == 1) register_result = 1;
    if (step == 2) notify_rsp_result = notify_no_rsp_result = 1;
    bool disconnect = false, established = false;
    assert(!advance(s, step, ESP_GATT_OK, &disconnect, &established));
    assert(disconnect && !established && !s.is_session_ready());
  }
  start(s, 2);
  notify_rsp_result = 1;
  bool established = false;
  assert(advance(s, 2, ESP_GATT_OK, nullptr, &established));
  assert(established && s.is_session_ready());
  start(s, 3);
  assert(advance(s, 3, ESP_GATT_OK, nullptr, &established));
  assert(established && s.is_session_ready());
  fake_time_us += 100000000;
  assert(!s.is_setup_timed_out(10000));
  std::cout << "Session setup failures, deadlines and recovery tests passed\n";
}
