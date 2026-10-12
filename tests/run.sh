#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT
for test in tests/*_test.cpp; do
  g++ -std=c++17 -Wall -Wextra -Werror -Itests/stubs -Iinclude src/session_client.cpp "$test" -lcrypto -o "$binary"
  "$binary"
done
