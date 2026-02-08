#include "SerialWjwwood.h"
#include <serial/serial.h>
#include <thread>
#include <chrono>

using namespace esctool;

SerialWjwwood::SerialWjwwood() = default;
SerialWjwwood::~SerialWjwwood() { close(); }

bool SerialWjwwood::open(const SerialOptions& opt) {
  try {
    s_ = std::make_unique<serial::Serial>(opt.port, opt.baud, serial::Timeout::simpleTimeout(opt.timeout_ms));
    if (s_->isOpen()) { last_opts_ = opt; return true; }
    return false;
  } catch (...) { s_.reset(); return false; }
}

bool SerialWjwwood::isOpen() const { return s_ && s_->isOpen(); }

void SerialWjwwood::close() { if (s_) { try { s_->close(); } catch (...) {} s_.reset(); } }

size_t SerialWjwwood::write(const uint8_t* data, size_t len) { return s_? s_->write(data, len) : 0; }
size_t SerialWjwwood::read (uint8_t* data, size_t len) { return s_? s_->read(data, len) : 0; }
void   SerialWjwwood::flush() { if (s_) s_->flush(); }

void SerialWjwwood::flush_input() {
  if (!s_) return;
  try { s_->flushInput(); } catch (...) {}
}

void SerialWjwwood::flush_output() {
  if (!s_) return;
  try { s_->flushOutput(); } catch (...) {}
}

bool SerialWjwwood::reopen() {
  if (last_opts_.port.empty()) return false;
  close();
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  return open(last_opts_);
}

SerialOptions SerialWjwwood::last_options() const { return last_opts_; }
