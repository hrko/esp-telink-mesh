# Host regression tests

Run `bash tests/run.sh` on Linux with g++ (C++17) and OpenSSL development headers
(`libssl-dev` on Ubuntu).

These tests compile the production session client against fake GATT and clock
APIs and an OpenSSL-backed AES adapter. They exercise authenticated pairing,
invalid lengths, tampered responses, wrong credentials, AES failures, missing and unrelated completion events,
timeout boundaries, reset, reconnect, successful completion, and immediate write
failure. The physical BLE stack is not validated by these host tests.
