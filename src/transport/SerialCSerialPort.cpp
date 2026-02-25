#include "SerialCSerialPort.h"
#include <CSerialPort/SerialPort.h>
#include <CSerialPort/SerialPortInfo.h>
#include <thread>
#include <chrono>

using namespace esctool;

SerialCSerialPort::SerialCSerialPort() = default;
SerialCSerialPort::~SerialCSerialPort() { close(); }

bool SerialCSerialPort::open(const SerialOptions& opt) {
  try {
    s_ = std::make_unique<itas109::CSerialPort>();
    s_->init(opt.port.c_str(),
             static_cast<int>(opt.baud),
             itas109::ParityNone,
             itas109::DataBits8,
             itas109::StopOne,
             itas109::FlowNone,
             4096);
    s_->setOperateMode(itas109::SynchronousOperate);
    s_->setReadIntervalTimeout(opt.timeout_ms);
    if (s_->open()) {
      last_opts_ = opt;
      return true;
    }
    s_.reset();
    return false;
  } catch (...) {
    s_.reset();
    return false;
  }
}

bool SerialCSerialPort::isOpen() const {
  return s_ && s_->isOpen();
}

void SerialCSerialPort::close() {
  if (s_) {
    try { s_->close(); } catch (...) {}
    s_.reset();
  }
}

size_t SerialCSerialPort::write(const uint8_t* data, size_t len) {
  if (!s_) return 0;
  int written = s_->writeData(data, static_cast<int>(len));
  return written > 0 ? static_cast<size_t>(written) : 0;
}

size_t SerialCSerialPort::read(uint8_t* data, size_t len) {
  if (!s_) return 0;
  int rd = s_->readData(data, static_cast<int>(len));
  return rd > 0 ? static_cast<size_t>(rd) : 0;
}

void SerialCSerialPort::flush() {
  if (s_) s_->flushBuffers();
}

void SerialCSerialPort::flush_input() {
  if (!s_) return;
  try { s_->flushReadBuffers(); } catch (...) {}
}

void SerialCSerialPort::flush_output() {
  if (!s_) return;
  try { s_->flushWriteBuffers(); } catch (...) {}
}

bool SerialCSerialPort::reopen() {
  if (last_opts_.port.empty()) return false;
  close();
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  return open(last_opts_);
}

SerialOptions SerialCSerialPort::last_options() const {
  return last_opts_;
}
