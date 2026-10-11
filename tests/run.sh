#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -Itests/stubs -Iinclude src/session_client.cpp tests/session_timeout_test.cpp -o "$binary"
"$binary"
