#include "esctool/Log.h"
#include <iostream>

using namespace esctool;

std::string Log::now() {
  using namespace std::chrono;
  auto t = system_clock::to_time_t(system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

void Log::print(const char* lvl, const std::string& s) const {
  auto& os = output_to_stderr_ ? std::cerr : std::cout;
  os << "[" << now() << " " << lvl << "] " << s << "\n";
}
