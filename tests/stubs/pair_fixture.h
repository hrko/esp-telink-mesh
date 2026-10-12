#pragma once
#include <array>
#include <cstdint>

// Identical name/password and an all-zero peer challenge.
// Authentication is AES-128-ECB(0, 0), in wire byte order.
inline constexpr std::array<uint8_t, 17> pair_response = {
    0x0d, 0, 0, 0, 0, 0, 0, 0, 0, 0x2e, 0x2b, 0x34, 0xca, 0x59, 0xfa, 0x4c, 0x88};
