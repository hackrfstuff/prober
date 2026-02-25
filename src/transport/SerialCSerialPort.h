#pragma once
#include "esctool/ISerial.h"
#include <memory>

namespace itas109 { class CSerialPort; }

namespace esctool {
class SerialCSerialPort : public ISerial {
  std::unique_ptr<itas109::CSerialPort> s_;
  SerialOptions last_opts_;
public:
  SerialCSerialPort();
  ~SerialCSerialPort() override;
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
