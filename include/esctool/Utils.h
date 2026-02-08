#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace esctool {
std::string hxd(const std::vector<uint8_t>& v);
uint16_t crc16_xmodem(const uint8_t* data, size_t len);
uint32_t crc32(const uint8_t* data, size_t len); // table built on first use
}
