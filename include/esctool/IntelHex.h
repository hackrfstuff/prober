#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <optional>

namespace esctool {
class IntelHexImage {
  std::map<uint32_t,uint8_t> data_;
  std::optional<uint32_t> min_, max_;
public:
  void load(const std::string& path);         // throws std::runtime_error
  std::vector<uint8_t> build(uint32_t start, uint32_t end) const; // [start,end)
  std::optional<uint32_t> min_addr() const { return min_; }
  std::optional<uint32_t> max_addr() const { return max_; }
};
}
