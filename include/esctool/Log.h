#pragma once
#include <string>
#include <cstdio>
#include <chrono>
#include <ctime>

namespace esctool {

enum class LogLevel { INFO=0, DEBUG=1, TRACE=2 };

class Log {
  LogLevel level_;
  bool output_to_stderr_{false};
public:
  explicit Log(LogLevel lvl = LogLevel::INFO) : level_(lvl) {}
  void set_level(LogLevel lvl) { level_ = lvl; }
  void set_output_stderr(bool enable) { output_to_stderr_ = enable; }
  LogLevel level() const { return level_; }
  bool want(LogLevel lvl) const { return static_cast<int>(lvl) <= static_cast<int>(level_); }
  static std::string now();
  void info (const std::string& s) { if (want(LogLevel::INFO))  print("INFO",  s); }
  void debug(const std::string& s) { if (want(LogLevel::DEBUG)) print("DEBUG", s); }
  void trace(const std::string& s) { if (want(LogLevel::TRACE)) print("TRACE", s); }
private:
  void print(const char* lvl, const std::string& s) const;
};

}
