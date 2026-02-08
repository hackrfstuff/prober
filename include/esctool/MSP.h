#pragma once
#include "esctool/ISerial.h"
#include "esctool/Log.h"
#include <vector>
#include <cstdint>

namespace esctool {
// MSP v1 command IDs
constexpr uint8_t MSP_API_VERSION     = 1;
constexpr uint8_t MSP_FC_VARIANT      = 2;
constexpr uint8_t MSP_FC_VERSION      = 3;
constexpr uint8_t MSP_SET_PASSTHROUGH = 245;

class MSP {
  ISerial& ser_; Log& log_; double timeout_s_;
public:
  MSP(ISerial& s, Log& l, double timeout_s=1.2) : ser_(s), log_(l), timeout_s_(timeout_s) {}
  void send(uint8_t cmd, const std::vector<uint8_t>& payload={});
  std::vector<uint8_t> recv(int expect_cmd=-1);
  std::vector<uint8_t> req(uint8_t cmd, const std::vector<uint8_t>& payload={});
private:
  static uint8_t checksum(uint8_t ln, uint8_t cmd, const std::vector<uint8_t>& p);
};
}
