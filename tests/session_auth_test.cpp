#include <esp_telink_mesh/v1/session_client.h>
#include <aes_alt.h>
#include <pair_fixture.h>
#include <cassert>
#include <iostream>
using namespace esp_telink_mesh::v1;

static void start(SessionClient &session, const char *password = "test") {
  session.configure("test", password, 0x211);
  esp_bd_addr_t address{};
  assert(session.set_link_context(1, 1, address));
  session.set_characteristic_handles(1, 2, 3);
  assert(session.begin_pairing());
  assert(session.handle_write_char_evt(3, ESP_GATT_OK, nullptr, nullptr));
}

int main() {
  for (unsigned length = 0; length < 17; ++length) {
    SessionClient session;
    start(session);
    bool disconnect = false;
    assert(!session.handle_read_char_evt(3, ESP_GATT_OK, pair_response.data(), length, &disconnect, nullptr));
    assert(disconnect && !session.is_session_ready());
  }
  for (unsigned byte = 0; byte < pair_response.size(); ++byte) {
    SessionClient session;
    start(session);
    auto invalid = pair_response;
    invalid[byte] ^= 1;
    bool disconnect = false;
    assert(!session.handle_read_char_evt(3, ESP_GATT_OK, invalid.data(), invalid.size(), &disconnect, nullptr));
    assert(disconnect && !session.is_session_ready());
  }
  for (bool crypto_failure : {false, true}) {
    SessionClient session;
    start(session, crypto_failure ? "test" : "wrong");
    aes_fail = crypto_failure;
    bool disconnect = false;
    assert(!session.handle_read_char_evt(3, ESP_GATT_OK, pair_response.data(), pair_response.size(), &disconnect, nullptr));
    assert(disconnect && !session.is_session_ready());
    aes_fail = false;
  }
  SessionClient session;
  start(session);
  assert(session.handle_read_char_evt(3, ESP_GATT_OK, pair_response.data(), pair_response.size(), nullptr, nullptr));
  assert(session.handle_reg_for_notify_evt(1, ESP_GATT_OK, nullptr, nullptr));
  bool established = false;
  assert(session.handle_write_char_evt(1, ESP_GATT_OK, nullptr, &established));
  assert(established && session.is_session_ready());
  std::cout << "Session authentication tests passed\n";
}
