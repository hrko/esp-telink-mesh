# esp-telink-mesh

ESP-IDF oriented Telink Mesh v1 session client library extracted from `esphome-smart-ledz`.

## Public Headers

- `esp_telink_mesh/v1/session_client.h`

## Notes

- Depends on ESP-IDF BLE GATT client APIs.
- Implements pairing/session lifecycle, packet encrypt/decrypt, and notify decode.
- Call `is_control_write_timed_out(timeout_ms)` regularly, including when the
  outgoing queue is empty. When it returns true, reset the session and disconnect
  the BLE link before pairing again. The ESPHome integration uses 10 seconds.
- Host regression tests: `bash tests/run.sh` (Linux, g++ with C++17).

## Doxygen

Generate API docs locally:

```bash
(cd docs && doxygen Doxyfile)
```

Open:

- `docs/.doxygen/html/index.html`

Generated documentation output under `docs/.doxygen/` is excluded from version control.

Docs are published with GitHub Pages via:

- `.github/workflows/deploy-pages.yml`

Set repository Pages source to `GitHub Actions` in repository settings.
