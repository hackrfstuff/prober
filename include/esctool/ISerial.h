#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esctool {
struct SerialOptions {
  std::string port;
  uint32_t baud{115200};
  uint32_t timeout_ms{30}; // read timeout
};

class ISerial {
public:
  virtual ~ISerial() = default;
  virtual bool open(const SerialOptions& opt) = 0;
  virtual bool isOpen() const = 0;
  virtual void close() = 0;
  virtual size_t write(const uint8_t* data, size_t len) = 0;
  virtual size_t read(uint8_t* data, size_t len) = 0; // return bytes read (<=len)
  virtual void flush() = 0;
  virtual void flush_input() {}   // discard pending input bytes
  virtual void flush_output() {}  // wait for output to drain
  virtual bool reopen() { return false; } // close + reopen with same settings
  virtual SerialOptions last_options() const { return {}; }
};
}
