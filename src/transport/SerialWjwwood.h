#pragma once
#include "esctool/ISerial.h"
#include <memory>

namespace serial { class Serial; }

namespace esctool {
class SerialWjwwood : public ISerial {
  std::unique_ptr<serial::Serial> s_;
  SerialOptions last_opts_;
public:
  SerialWjwwood();
  ~SerialWjwwood() override;
  bool open(const SerialOptions& opt) override;
  bool isOpen() const override;
  void close() override;
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* data, size_t len) override;
  void flush() override;
  void flush_input() override;
  void flush_output() override;
  bool reopen() override;
  SerialOptions last_options() const override;
};
}
