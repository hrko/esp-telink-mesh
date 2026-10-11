# Host regression tests

Run `bash tests/run.sh` on Linux with g++ (C++17).

These tests compile the production session client against fake GATT, clock, and
AES APIs. They exercise pairing, missing and unrelated completion events,
timeout boundaries, reset, reconnect, successful completion, and immediate write
failure. AES and the physical BLE stack are not validated by these host tests.
