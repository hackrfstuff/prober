#include "esctool/Log.h"
#include "esctool/ISerial.h"
#include "esctool/MSP.h"
#include "esctool/FourWay.h"
#include "esctool/Flasher.h"
#include "esctool/Bluejay.h"
#include "esctool/BluejaySettings.h"
#include "esctool/C2Flasher.h"
#include "esctool/IntelHex.h"
#include "esctool/version.h"
#include "transport/SerialCSerialPort.h"

#include <iostream>
#include <thread>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <vector>
#include <CSerialPort/SerialPortInfo.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace esctool;

static std::string pick_port(Log& log, const std::string& forced){
  if (!forced.empty()) return forced;
  try{
    std::vector<std::string> picks; std::string first;
    auto ports = itas109::CSerialPortInfo::availablePortInfos();
    for (auto& p : ports) {
      std::string portName = p.portName;
      std::string desc = p.description;
      std::string hwid = p.hardwareId;
      if (first.empty()) first = portName;
      for (auto& c:hwid) c = (char)toupper(c);
      std::string vid, pid; auto pos = hwid.find("VID_");
      if (pos!=std::string::npos){
        vid = hwid.substr(pos+4, 4);
        auto pidpos = hwid.find("PID_");
        if (pidpos!=std::string::npos) pid = hwid.substr(pidpos+4, 4);
      }
      if (!vid.empty() && !pid.empty()){
        if ((vid=="0483"&&pid=="5740") || (vid=="10C4"&&pid=="EA60") || (vid=="1A86"&&pid=="7523")) picks.push_back(portName);
      }
      log.info("Port: "+portName+" - "+desc + (vid.empty()? std::string("") : (" [VID_"+vid+"&PID_"+pid+"]")));
    }
    if (!picks.empty()) return picks.front();
    return first;
  } catch(...) { return std::string(); }
}

struct Args {
  bool show_version=false;
  std::string port; int baud=115200; double timeout=1.0; double delay=0.0; bool timeout_user_set=false; bool delay_user_set=false; LogLevel level=LogLevel::INFO; bool trace=false; int probe_tries=6; double probe_sleep=0.15;
  int settle_ms=200;
  std::string hex; int index=-1; bool all=false; VerifyMode verify_mode=VerifyMode::NONE; bool erase_eeprom=false; std::string assume_sig;
  bool safe=false;
  std::string mapping="auto";
  std::string settings="preserve";
  bool list_ports=false;
  bool json=false;
  bool ui_json=false;
  bool read=false;
  int read_rounds=2;
  int read_round_sleep_ms=150;
  bool write_settings=false;
  std::vector<std::string> set_args;
  bool probe_escs=false;
  bool probe_sweep=false;
  bool skip_missing=false;
  bool full_erase_app=false;
  bool full_erase_entire_app=false;
  bool dry_run=false;
  bool verify_all_bytes=false;
  bool c2=false;
  std::string c2_port;
  bool c2_detect=false;
  bool c2_read_info=false;
  std::string c2_write_hex;
  bool c2_erase=false;
  bool c2_reset=false;
  std::string c2_install;
  std::string c2_install_hex;
  int c2_timeout_ms=2000;
  int c2_connect_delay_ms=2000;
  std::string bluejay_version = "0.21.0";
  std::string bluejay_dir;
  int flash_inter_esc_ms=250;
  int flash_preselect_tries=2;
  int flash_preselect_sleep_ms=150;
  int flash_post_select_ms=200;
  int flash_erase_retries=3;
  int flash_erase_inter_page_ms=50;
  int flash_write_retries=3;
  int flash_write_inter_block_ms=10;
  int verify_read_retries=3;
  int ui_esc_index=-1;
};

static Args parse_args(int argc, char** argv){
  Args a; for(int i=1;i<argc;++i){
    auto eq = [&](const char* k){ return std::strcmp(argv[i],k)==0; };
    auto next = [&](){ return (i+1<argc)? argv[++i] : (char*)""; };
    if (eq("--version")) a.show_version = true;
    else if (eq("--list-ports")) a.list_ports = true;
    else if (eq("--json")) a.json = true;
    else if (eq("--ui-json")) a.ui_json = true;
    if (eq("--port")) a.port = next();
    else if (eq("--baud")) a.baud = std::atoi(next());
    else if (eq("--timeout")) { a.timeout = std::atof(next()); a.timeout_user_set = true; }
    else if (eq("--delay")) { a.delay = std::atof(next()); a.delay_user_set = true; }
    else if (eq("--loglevel")) { std::string s=next(); if(s=="DEBUG") a.level=LogLevel::DEBUG; else if(s=="TRACE") a.level=LogLevel::TRACE; else a.level=LogLevel::INFO; }
    else if (eq("--trace")) a.trace = true;
    else if (eq("--probe-tries")) a.probe_tries = std::atoi(next());
    else if (eq("--probe-sleep")) a.probe_sleep = std::atof(next());
    else if (eq("--settle-ms")) a.settle_ms = std::atoi(next());
    else if (eq("--hex")) a.hex = next();
    else if (eq("--index")) a.index = std::atoi(next());
    else if (eq("--all")) a.all = true;
    else if (eq("--verify")){
      if (i+1<argc && argv[i+1][0] != '-') {
        std::string mode = next();
        if      (mode=="fast" || mode=="FAST") a.verify_mode = VerifyMode::FAST;
        else if (mode=="full" || mode=="FULL") a.verify_mode = VerifyMode::FULL;
        else if (mode=="off"  || mode=="OFF" || mode=="none" || mode=="NONE") a.verify_mode = VerifyMode::NONE;
        else { a.verify_mode = VerifyMode::FULL; }
      } else {
        a.verify_mode = VerifyMode::FULL;
      }
    }
    else if (eq("--erase-eeprom")) a.erase_eeprom = true;
    else if (eq("--safe")) a.safe = true;
    else if (eq("--mapping")) a.mapping = next();
    else if (eq("--settings")) a.settings = next();
    else if (eq("--read")) a.read = true;
    else if (eq("--read-rounds")) a.read_rounds = std::atoi(next());
    else if (eq("--read-round-sleep-ms")) a.read_round_sleep_ms = std::atoi(next());
    else if (eq("--write-settings")) a.write_settings = true;
    else if (eq("--set")) a.set_args.push_back(next());
    else if (eq("--probe-escs")) a.probe_escs = true;
    else if (eq("--probe-sweep")) a.probe_sweep = true;
    else if (eq("--skip-missing")) a.skip_missing = true;
    else if (eq("--full-erase-app")) a.full_erase_app = true;
    else if (eq("--full-erase-entire-app")) a.full_erase_entire_app = true;
    else if (eq("--dry-run")) a.dry_run = true;
    else if (eq("--verify-all-bytes")) a.verify_all_bytes = true;
    else if (eq("--assume-sig")) a.assume_sig = next();
    // C2 options
    else if (eq("--c2")) a.c2 = true;
    else if (eq("--c2-port")) a.c2_port = next();
    else if (eq("--c2-detect")) a.c2_detect = true;
    else if (eq("--c2-read-info")) a.c2_read_info = true;
    else if (eq("--c2-write-hex")) a.c2_write_hex = next();
    else if (eq("--c2-erase")) a.c2_erase = true;
    else if (eq("--c2-reset")) a.c2_reset = true;
    else if (eq("--c2-install")) a.c2_install = next();
    else if (eq("--c2-install-hex")) a.c2_install_hex = next();
    else if (eq("--bluejay-version")) a.bluejay_version = next();
    else if (eq("--bluejay-dir")) a.bluejay_dir = next();
    else if (eq("--c2-timeout-ms")) a.c2_timeout_ms = std::atoi(next());
    else if (eq("--c2-connect-delay-ms")) a.c2_connect_delay_ms = std::atoi(next());
    else if (eq("--flash-inter-esc-ms")) a.flash_inter_esc_ms = std::atoi(next());
    else if (eq("--flash-preselect-tries")) a.flash_preselect_tries = std::atoi(next());
    else if (eq("--flash-preselect-sleep-ms")) a.flash_preselect_sleep_ms = std::atoi(next());
    else if (eq("--flash-post-select-ms")) a.flash_post_select_ms = std::atoi(next());
    else if (eq("--flash-erase-retries")) a.flash_erase_retries = std::atoi(next());
    else if (eq("--flash-erase-inter-page-ms")) a.flash_erase_inter_page_ms = std::atoi(next());
    else if (eq("--flash-write-retries")) a.flash_write_retries = std::atoi(next());
    else if (eq("--flash-write-inter-block-ms")) a.flash_write_inter_block_ms = std::atoi(next());
    else if (eq("--verify-read-retries")) a.verify_read_retries = std::atoi(next());
    else if (eq("--ui-esc-index")) a.ui_esc_index = std::atoi(next());
  }
  return a;
}

static std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04X", (unsigned char)c);
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

static void ndjson_emit(bool enable, const std::string& line) {
  if (!enable) return;
  std::cout << line << "\n";
  std::cout.flush();
}

static std::string json_bool(bool v) { return v ? "true" : "false"; }

static std::string path_basename(const std::string& p) {
  size_t a = p.find_last_of('/');
  size_t b = p.find_last_of('\\');
  size_t pos = std::string::npos;
  if (a != std::string::npos && b != std::string::npos) pos = std::max(a, b);
  else if (a != std::string::npos) pos = a;
  else pos = b;
  if (pos == std::string::npos) return p;
  return p.substr(pos + 1);
}

// Normalize ESC target string to slug matching bundled hex filenames
// e.g. "#A_X_05#" -> "A_X_5"
static std::string normalize_target_slug(const std::string& target) {
  std::string s;
  for (char c : target) {
    if (c == '#') continue;
    if (c == '-') s += '_';
    else s += c;
  }
  // Split by '_', strip leading zeros from last numeric token
  std::vector<std::string> parts;
  std::string tok;
  for (char c : s) {
    if (c == '_') { if (!tok.empty()) parts.push_back(tok); tok.clear(); }
    else tok += c;
  }
  if (!tok.empty()) parts.push_back(tok);
  if (!parts.empty()) {
    bool all_digit = true;
    for (char c : parts.back()) { if (!isdigit((unsigned char)c)) { all_digit = false; break; } }
    if (all_digit && !parts.back().empty()) {
      parts.back() = std::to_string(std::atoi(parts.back().c_str()));
    }
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += '_';
    out += parts[i];
  }
  return out;
}

// Resolve bundled Bluejay hex path given target slug, pwm, and version
static std::string resolve_bundled_bluejay_hex(const std::string& exe_dir,
                                                const std::string& bluejay_dir_override,
                                                const std::string& target_slug,
                                                int pwm_khz,
                                                const std::string& version,
                                                Log& log) {
  if (pwm_khz != 24 && pwm_khz != 48 && pwm_khz != 96) return {};
  std::string filename = target_slug + "_" + std::to_string(pwm_khz) + "_v" + version + ".hex";
  std::string ver_dir = "v" + version;

  std::vector<std::string> search_bases;
  if (!bluejay_dir_override.empty()) {
    search_bases.push_back(bluejay_dir_override + "/" + ver_dir);
  }
  if (!exe_dir.empty()) {
    search_bases.push_back(exe_dir + "/tools/bluejay_firmware/" + ver_dir);
    search_bases.push_back(exe_dir + "/../bluejay_firmware/" + ver_dir);
    search_bases.push_back(exe_dir + "/../../tools/bluejay_firmware/" + ver_dir);
  }
  search_bases.push_back("tools/bluejay_firmware/" + ver_dir);

  for (const auto& base : search_bases) {
    std::string candidate = base + "/" + filename;
    std::replace(candidate.begin(), candidate.end(), '\\', '/');
    std::ifstream check(candidate);
    if (check.good()) {
      log.info("Bluejay auto: found " + candidate);
      return candidate;
    }
    log.trace("Bluejay auto: tried " + candidate + " (not found)");
  }
  return {};
}

static void emit_port_list_json() {
  auto ports = itas109::CSerialPortInfo::availablePortInfos();
  std::cout << "[";
  bool first = true;
  for (auto& p : ports) {
    if (!first) std::cout << ",";
    first = false;
    std::cout << "{\"port\":\"" << json_escape(p.portName) << "\"";
    std::cout << ",\"description\":\"" << json_escape(p.description) << "\"";
    std::cout << ",\"hwid\":\"" << json_escape(p.hardwareId) << "\"}";
  }
  std::cout << "]\n";
}

int main(int argc, char** argv){
  auto args = parse_args(argc, argv);

  if (args.show_version) {
    std::cout << "prober " << PROBER_VERSION << "\n";
    return 0;
  }

  if (args.list_ports) {
    try {
      if (args.json) {
        emit_port_list_json();
      } else {
        auto ports = itas109::CSerialPortInfo::availablePortInfos();
        for (auto& p : ports) {
          std::cout << p.portName;
          if (p.description[0]) std::cout << " - " << p.description;
          if (p.hardwareId[0]) std::cout << " (" << p.hardwareId << ")";
          std::cout << "\n";
        }
      }
      return 0;
    } catch (...) {
      return 1;
    }
  }

  // ========== C2 MODE ==========
  if (args.c2) {
    Log log(args.trace ? LogLevel::TRACE : args.level);
    if (args.ui_json) {
      log.set_output_stderr(true);
      ndjson_emit(true, std::string("{\"type\":\"ui_hello\",\"tool\":\"bluejay_flasher\",\"pid\":") +
                        std::to_string(
#ifdef _WIN32
                            GetCurrentProcessId()
#else
                            getpid()
#endif
                        ) + "}");
    }

    if (args.c2_port.empty()) {
      std::cerr << "--c2-port is required for C2 mode\n";
      return 2;
    }

    // --c2-install: Handle BEFORE opening serial port - avrdude needs to control reset timing
    if (!args.c2_install.empty()) {
      std::string board = args.c2_install;
      if (board != "uno" && board != "nano") {
        std::cerr << "--c2-install must be 'uno' or 'nano'\n";
        return 2;
      }

      ndjson_emit(args.ui_json, "{\"type\":\"c2_install_start\",\"port\":\"" + json_escape(args.c2_port) + 
                  "\",\"board\":\"" + board + "\"}");

      // Find the hex file
      std::string hex_path;
      if (!args.c2_install_hex.empty()) {
        std::ifstream check(args.c2_install_hex);
        if (check.good()) {
          hex_path = args.c2_install_hex;
          std::replace(hex_path.begin(), hex_path.end(), '\\', '/');
          log.info("C2: Using explicit firmware path: " + hex_path);
        } else {
          log.info("C2: Specified firmware not found: " + args.c2_install_hex);
        }
      } else {
        std::string exe_dir;
#ifdef _WIN32
        char path_buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, path_buf, MAX_PATH);
        if (len > 0) {
          exe_dir = std::string(path_buf);
          size_t last_sep = exe_dir.find_last_of("\\/");
          if (last_sep != std::string::npos) exe_dir = exe_dir.substr(0, last_sep);
        }
#else
        char path_buf[4096];
        ssize_t len = readlink("/proc/self/exe", path_buf, sizeof(path_buf) - 1);
        if (len > 0) {
          path_buf[len] = '\0';
          exe_dir = std::string(path_buf);
          size_t last_sep = exe_dir.find_last_of('/');
          if (last_sep != std::string::npos) exe_dir = exe_dir.substr(0, last_sep);
        }
#endif
        std::vector<std::string> hex_search_paths;
        if (!exe_dir.empty()) {
          hex_search_paths.push_back(exe_dir + "/tools/c2_firmware/uno_nano.hex");
          hex_search_paths.push_back(exe_dir + "/../tools/c2_firmware/uno_nano.hex");
          hex_search_paths.push_back(exe_dir + "/../../tools/c2_firmware/uno_nano.hex");
        }
        hex_search_paths.push_back("tools/c2_firmware/uno_nano.hex");

        for (const auto& p : hex_search_paths) {
          std::ifstream check(p);
          if (check.good()) {
            hex_path = p;
            std::replace(hex_path.begin(), hex_path.end(), '\\', '/');
            log.info("C2: Found interface firmware at " + hex_path);
            break;
          } else {
            log.trace("C2: Not found: " + p);
          }
        }
      }
      
      if (hex_path.empty()) {
        log.info("C2: Interface firmware not found");
        ndjson_emit(args.ui_json, "{\"type\":\"c2_install_fail\",\"error\":\"Interface firmware not found\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_install\",\"success\":false}");
        return 1;
      }

      // Find avrdude
      std::string avrdude = "avrdude";
      std::string avrdude_conf;
      {
        std::string exe_dir;
        std::vector<std::string> search_paths;
#ifdef _WIN32
        char path_buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, path_buf, MAX_PATH);
        if (len > 0) {
          exe_dir = std::string(path_buf);
          size_t last_sep = exe_dir.find_last_of("\\/");
          if (last_sep != std::string::npos) exe_dir = exe_dir.substr(0, last_sep);
        }
        if (!exe_dir.empty()) {
          search_paths.push_back(exe_dir + "\\tools\\avrdude\\avrdude.exe");
          search_paths.push_back(exe_dir + "\\..\\tools\\avrdude\\avrdude.exe");
          search_paths.push_back(exe_dir + "\\..\\..\\tools\\avrdude\\avrdude.exe");
        }
        search_paths.push_back("tools\\avrdude\\avrdude.exe");
#else
        char path_buf[4096];
        ssize_t len = readlink("/proc/self/exe", path_buf, sizeof(path_buf) - 1);
        if (len > 0) {
          path_buf[len] = '\0';
          exe_dir = std::string(path_buf);
          size_t last_sep = exe_dir.find_last_of('/');
          if (last_sep != std::string::npos) exe_dir = exe_dir.substr(0, last_sep);
        }
        if (!exe_dir.empty()) {
          search_paths.push_back(exe_dir + "/tools/avrdude/avrdude");
          search_paths.push_back(exe_dir + "/../tools/avrdude/avrdude");
          search_paths.push_back(exe_dir + "/../../tools/avrdude/avrdude");
        }
        search_paths.push_back("tools/avrdude/avrdude");
#endif
        
        for (const auto& p : search_paths) {
          std::ifstream check(p);
          if (check.good()) {
            std::string avrdude_path = p;
            std::replace(avrdude_path.begin(), avrdude_path.end(), '\\', '/');
            avrdude = "\"" + avrdude_path + "\"";
            std::string conf_path = p;
#ifdef _WIN32
            size_t exe_pos = conf_path.rfind("avrdude.exe");
            if (exe_pos != std::string::npos) {
              conf_path = conf_path.substr(0, exe_pos) + "avrdude.conf";
#else
            size_t exe_pos = conf_path.rfind("avrdude");
            if (exe_pos != std::string::npos) {
              conf_path = conf_path.substr(0, exe_pos) + "avrdude_linux.conf";
#endif
              std::ifstream conf_check(conf_path);
              if (conf_check.good()) {
                std::replace(conf_path.begin(), conf_path.end(), '\\', '/');
                avrdude_conf = " -C \"" + conf_path + "\"";
              }
            }
            log.info("C2: Found local avrdude at " + avrdude_path);
            break;
          }
        }
      }

      std::string mcu = "atmega328p";
      std::string programmer = "arduino";
      
      std::vector<int> baud_rates = {115200, 57600};
      int ret = 1;
      
      for (int upload_baud : baud_rates) {
#ifdef _WIN32
        std::string flash_arg = "\"flash:w:" + hex_path + ":i\"";
        std::string cmd = "cmd /c \"" + avrdude + avrdude_conf + " -p " + mcu + " -c " + programmer + 
                          " -P " + args.c2_port + " -b " + std::to_string(upload_baud) +
                          " -U " + flash_arg + "\"";
#else
        std::string flash_arg = "flash:w:" + hex_path + ":i";
        std::string cmd = avrdude + avrdude_conf + " -p " + mcu + " -c " + programmer + 
                          " -P " + args.c2_port + " -b " + std::to_string(upload_baud) +
                          " -U " + flash_arg;
#endif

        log.info("C2: Trying baud " + std::to_string(upload_baud) + "...");
        ndjson_emit(args.ui_json, "{\"type\":\"c2_install_progress\",\"step\":\"upload\",\"message\":\"Trying baud " + std::to_string(upload_baud) + "...\"}");

        ret = std::system(cmd.c_str());
        if (ret == 0) break;
        log.info("C2: Baud " + std::to_string(upload_baud) + " failed, trying next...");
      }

      if (ret != 0) {
        log.info("C2: avrdude failed");
        ndjson_emit(args.ui_json, "{\"type\":\"c2_install_fail\",\"error\":\"avrdude failed - check Arduino connection\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_install\",\"success\":false}");
        return 1;
      }

      log.info("C2: Install OK");
      ndjson_emit(args.ui_json, "{\"type\":\"c2_install_ok\",\"port\":\"" + json_escape(args.c2_port) + 
                  "\",\"board\":\"" + board + "\"}");
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_install\",\"success\":true}");
      return 0;
    }

    // Open serial at 1,000,000 baud for C2 (for non-install operations)
    SerialCSerialPort serial;
    SerialOptions so;
    so.port = args.c2_port;
    so.baud = 1000000;
    so.timeout_ms = 100;

    if (!serial.open(so)) {
      log.info("C2: Could not open " + so.port);
      ndjson_emit(args.ui_json, "{\"type\":\"c2_detect_fail\",\"port\":\"" + json_escape(so.port) + 
                  "\",\"error\":\"Could not open port\"}");
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_detect\",\"success\":false}");
      return 1;
    }
    log.info("C2: Opened " + so.port + " @ 1000000 baud");

    // Wait for Arduino to reset after opening port
    if (args.c2_connect_delay_ms > 0) {
      log.info("C2: Waiting " + std::to_string(args.c2_connect_delay_ms) + "ms for Arduino reset...");
      std::this_thread::sleep_for(std::chrono::milliseconds(args.c2_connect_delay_ms));
    }

    C2Flasher c2(serial, log);
    c2.set_timeout_ms(args.c2_timeout_ms);

    // --c2-detect: Check if C2 interface is present
    if (args.c2_detect) {
      ndjson_emit(args.ui_json, "{\"type\":\"c2_detect_start\",\"port\":\"" + json_escape(so.port) + "\",\"baud\":1000000}");
      bool has_iface = c2.has_interface();
      if (has_iface) {
        log.info("C2: Interface detected");
        ndjson_emit(args.ui_json, "{\"type\":\"c2_detect_ok\",\"port\":\"" + json_escape(so.port) + "\",\"baud\":1000000}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_detect\",\"success\":true}");
        return 0;
      } else {
        log.info("C2: Interface NOT detected - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_detect_fail\",\"port\":\"" + json_escape(so.port) + 
                    "\",\"baud\":1000000,\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_detect\",\"success\":false}");
        return 1;
      }
    }

    // --c2-read-info: Initialize and get device info
    if (args.c2_read_info) {
      ndjson_emit(args.ui_json, "{\"type\":\"c2_read_info_start\"}");
      
      if (!c2.initialize()) {
        log.info("C2: Initialize failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_read_info_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_read_info\",\"success\":false}");
        return 1;
      }
      log.info("C2: Initialized");

      auto info = c2.get_device_info();
      if (!info) {
        log.info("C2: Get device info failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_read_info_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_read_info\",\"success\":false}");
        return 1;
      }

      log.info("C2: Device ID=" + info->device_id_hex + " Revision=" + info->revision_hex);
      
      // Check for 0xFF which indicates initialization failure
      if (info->device_id == 0xFF && info->revision == 0xFF) {
        log.info("C2: Warning - Device ID and Revision are 0xFF, target may not be connected");
        ndjson_emit(args.ui_json, "{\"type\":\"c2_target_info\",\"device_id\":\"" + info->device_id_hex + 
                    "\",\"revision\":\"" + info->revision_hex + "\",\"warning\":\"target_not_connected\"}");
      } else {
        ndjson_emit(args.ui_json, "{\"type\":\"c2_target_info\",\"device_id\":\"" + info->device_id_hex + 
                    "\",\"revision\":\"" + info->revision_hex + "\"}");
      }
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_read_info\",\"success\":true}");
      return 0;
    }

    // --c2-erase: Erase target
    if (args.c2_erase) {
      ndjson_emit(args.ui_json, "{\"type\":\"c2_erase_start\"}");
      
      if (!c2.initialize()) {
        log.info("C2: Initialize failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_erase_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_erase\",\"success\":false}");
        return 1;
      }

      if (!c2.erase()) {
        log.info("C2: Erase failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_erase_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_erase\",\"success\":false}");
        return 1;
      }

      log.info("C2: Erase OK");
      ndjson_emit(args.ui_json, "{\"type\":\"c2_erase_ok\"}");
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_erase\",\"success\":true}");
      return 0;
    }

    // --c2-reset: Reset target
    if (args.c2_reset) {
      if (!c2.reset()) {
        log.info("C2: Reset failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_reset_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_reset\",\"success\":false}");
        return 1;
      }
      log.info("C2: Reset OK");
      ndjson_emit(args.ui_json, "{\"type\":\"c2_reset_ok\"}");
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_reset\",\"success\":true}");
      return 0;
    }

    // --c2-write-hex: Erase and write hex file to target
    if (!args.c2_write_hex.empty()) {
      std::string hex_path = args.c2_write_hex;
      std::string hex_name = path_basename(hex_path);

      // Load hex file
      IntelHexImage hex;
      try {
        hex.load(hex_path);
      } catch (const std::exception& e) {
        log.info("C2: Failed to load hex file: " + std::string(e.what()));
        ndjson_emit(args.ui_json, "{\"type\":\"c2_write_fail\",\"error\":\"" + json_escape(e.what()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_write\",\"success\":false}");
        return 1;
      }

      if (!hex.min_addr() || !hex.max_addr()) {
        log.info("C2: Hex file is empty");
        ndjson_emit(args.ui_json, "{\"type\":\"c2_write_fail\",\"error\":\"Hex file is empty\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_write\",\"success\":false}");
        return 1;
      }

      uint32_t start = *hex.min_addr();
      uint32_t end = *hex.max_addr() + 1;
      auto data = hex.build(start, end);
      size_t total_bytes = data.size();

      // Build address->byte pairs for write_hex_image
      std::vector<std::pair<uint32_t, uint8_t>> hex_data;
      hex_data.reserve(total_bytes);
      for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] != 0xFF) {  // Skip unprogrammed bytes
          hex_data.push_back({start + i, data[i]});
        }
      }

      log.info("C2: Writing " + hex_name + " (" + std::to_string(hex_data.size()) + " bytes)");
      
      ndjson_emit(args.ui_json, "{\"type\":\"c2_write_plan\",\"hex_path\":\"" + json_escape(hex_path) + 
                  "\",\"hex_name\":\"" + json_escape(hex_name) + 
                  "\",\"total_bytes\":" + std::to_string(hex_data.size()) + "}");

      // Initialize
      if (!c2.initialize()) {
        log.info("C2: Initialize failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_write_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_write\",\"success\":false}");
        return 1;
      }

      // Erase
      ndjson_emit(args.ui_json, "{\"type\":\"c2_erase_start\"}");
      log.info("C2: Erasing...");
      if (!c2.erase()) {
        log.info("C2: Erase failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_erase_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_write\",\"success\":false}");
        return 1;
      }
      log.info("C2: Erase OK");
      ndjson_emit(args.ui_json, "{\"type\":\"c2_erase_ok\"}");

      // Write with progress callback
      bool ui_json = args.ui_json;
      int ui_esc_idx = args.ui_esc_index;
      bool write_ok = c2.write_hex_image(hex_data, [&](size_t bytes_done, size_t bytes_total, size_t chunk_idx, size_t chunks_total) {
        ndjson_emit(ui_json, "{\"type\":\"c2_write_progress\",\"bytes_done\":" + std::to_string(bytes_done) +
                    ",\"bytes_total\":" + std::to_string(bytes_total) +
                    ",\"chunk_index\":" + std::to_string(chunk_idx) +
                    ",\"chunks_total\":" + std::to_string(chunks_total) +
                    (ui_esc_idx >= 0 ? ",\"esc_index\":" + std::to_string(ui_esc_idx) : "") + "}");
      });

      if (!write_ok) {
        log.info("C2: Write failed - " + c2.last_error());
        ndjson_emit(args.ui_json, "{\"type\":\"c2_write_fail\",\"error\":\"" + json_escape(c2.last_error()) + "\"}");
        ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_write\",\"success\":false}");
        return 1;
      }

      log.info("C2: Write OK");
      ndjson_emit(args.ui_json, "{\"type\":\"c2_write_ok\"" + 
                  (args.ui_esc_index >= 0 ? ",\"esc_index\":" + std::to_string(args.ui_esc_index) : "") + "}");
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"c2_write\",\"success\":true}");
      return 0;
    }

    std::cerr << "C2 mode requires one of: --c2-detect, --c2-read-info, --c2-erase, --c2-reset, --c2-write-hex, --c2-install\n";
    return 2;
  }
  // ========== END C2 MODE ==========

  if (args.index >= 0 && args.index < 1) { std::cerr << "--index is 1-based; must be >= 1\n"; return 2; }
  bool no_flash_mode = args.probe_escs || args.probe_sweep || args.read || args.write_settings;
  if (args.read && args.index < 0 && !args.all) args.all = true;
  if (args.write_settings && args.index < 0 && !args.all) args.all = true;
  if (args.hex.empty() && !no_flash_mode){ std::cerr << "--hex <path|auto> is required (unless using --read, --write-settings, --probe-escs or --probe-sweep)\n"; return 2; }
  if (args.index<0 && !args.all && !no_flash_mode){ std::cerr << "Specify either --index N (1-based) or --all (or use --read/--write-settings/--probe-escs/--probe-sweep)\n"; return 2; }

  // Parse --set args into a SettingsPatch
  SettingsPatch settings_patch;
  for (const auto& sa : args.set_args) {
    std::string err;
    if (!bj_parse_set_arg(sa, &settings_patch, &err)) {
      std::cerr << "Invalid --set argument '" << sa << "': " << err << "\n";
      return 2;
    }
  }

  Log log(args.trace? LogLevel::TRACE : args.level);
  if (args.ui_json) {
    log.set_output_stderr(true);
    // Emit ui_hello handshake early so UI knows NDJSON is alive
    ndjson_emit(true, std::string("{\"type\":\"ui_hello\",\"tool\":\"bluejay_flasher\",\"pid\":") +
                          std::to_string(
#ifdef _WIN32
                              GetCurrentProcessId()
#else
                              getpid()
#endif
                          ) + "}");
  }

  double timeout_s = args.timeout;
  double delay_s = args.delay;
  bool applied_safe_defaults = false;
  if (args.safe) {
    if (!args.delay_user_set) { delay_s = 0.010; applied_safe_defaults = true; }
    if (!args.timeout_user_set) { timeout_s = 6.0; applied_safe_defaults = true; }
    if (applied_safe_defaults) {
      log.info("BB51 safe defaults: delay=0.010s timeout=6s (override with --delay/--timeout)");
    }
  }

  SerialCSerialPort serial; SerialOptions so; so.port = pick_port(log, args.port); so.baud=args.baud; so.timeout_ms=30;
  if (so.port.empty()){ log.info("No serial ports found."); return 1; }
  if (!serial.open(so)){ log.info(std::string("Could not open ")+so.port); return 1; }
  log.info("Opened "+so.port+" @ "+std::to_string(args.baud)+" baud");

  ndjson_emit(args.ui_json,
              std::string("{\"type\":\"port_open\",\"port\":\"") + json_escape(so.port) +
              "\",\"baud\":" + std::to_string(args.baud) + "}");

  MSP msp(serial, log, 1.2);
  FourWay fw(serial, log, timeout_s, args.trace, delay_s);
  fw.set_probe_sleep(args.probe_sleep);
  Flasher fl(serial, msp, fw, log);
  fl.probe_tries = args.probe_tries;
  fl.probe_sleep = args.probe_sleep;
  fl.safe_mode = args.safe;
  fl.full_erase_app = args.full_erase_app;
  fl.full_erase_entire_app = args.full_erase_entire_app;
  fl.dry_run = args.dry_run;
  fl.verify_all_bytes = args.verify_all_bytes;
  fl.delay_user_set = args.delay_user_set;
  fl.timeout_user_set = args.timeout_user_set;
  fl.erase_retries = args.flash_erase_retries;
  fl.erase_inter_page_ms = args.flash_erase_inter_page_ms;
  fl.write_retries = args.flash_write_retries;
  fl.write_inter_block_ms = args.flash_write_inter_block_ms;
  fl.verify_read_retries = args.verify_read_retries;

  {
    try {
      auto api = msp.req(MSP_API_VERSION);
      int api_maj = api.size() >= 1 ? api[0] : -1;
      int api_min = api.size() >= 2 ? api[1] : -1;
      std::string variant;
      try {
        auto v = msp.req(MSP_FC_VARIANT);
        variant.assign((char*)v.data(), v.size());
      } catch (...) {}
      std::string version;
      try {
        auto fv = msp.req(MSP_FC_VERSION);
        if (fv.size() >= 3) {
          version = std::to_string(fv[0]) + "." + std::to_string(fv[1]) + "." + std::to_string(fv[2]);
        }
      } catch (...) {}
      std::ostringstream oss;
      oss << "{\"type\":\"msp_info\"";
      if (api_maj >= 0 && api_min >= 0) oss << ",\"api_version\":\"" << api_maj << "." << api_min << "\"";
      if (!variant.empty()) oss << ",\"fc_variant\":\"" << json_escape(variant) << "\"";
      if (!version.empty()) oss << ",\"fc_version\":\"" << json_escape(version) << "\"";
      oss << "}";
      ndjson_emit(args.ui_json, oss.str());
    } catch (...) {
      // Ignore MSP info errors; enter_passthrough() will handle handshake.
    }
  }

  auto esc_count_opt = fl.enter_passthrough(); if (!esc_count_opt.has_value()) return 1; int esc_count = esc_count_opt.value_or(-1);
  if (!fl.bringup_4way()) return 1;

  if (esc_count > 0) {
    ndjson_emit(args.ui_json,
                std::string("{\"type\":\"passthrough_info\",\"esc_count\":") + std::to_string(esc_count) + "}");
  }

  auto exit_passthrough = [&]() -> MspRestoreResult {
    auto result = fl.exit_passthrough_and_restore_msp(5000);
    ndjson_emit(args.ui_json,
                std::string("{\"type\":\"msp_restore\",\"ok\":") + (result.ok ? "true" : "false") +
                ",\"attempts\":" + std::to_string(result.attempts) +
                ",\"elapsed_ms\":" + std::to_string(result.elapsed_ms) +
                ",\"last_error\":\"" + json_escape(result.last_error) + "\"}");
    return result;
  };

  auto exit_and_return = [&](int code) {
    exit_passthrough();
    return code;
  };

  if (args.settle_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(args.settle_ms));
  }

  MappingMode mapping_mode = MappingMode::AUTO;
  if (args.mapping == "auto") mapping_mode = MappingMode::AUTO;
  else if (args.mapping == "prefer-0-based") mapping_mode = MappingMode::PREFER_0_BASED;
  else if (args.mapping == "prefer-1-based") mapping_mode = MappingMode::PREFER_1_BASED;
  else if (args.mapping == "strict-0-based" || args.mapping == "0-based") mapping_mode = MappingMode::STRICT_0_BASED;
  else if (args.mapping == "strict-1-based" || args.mapping == "1-based") mapping_mode = MappingMode::STRICT_1_BASED;

  auto mapping_mode_str = [&](MappingMode m)->std::string{
    switch(m){
      case MappingMode::AUTO: return "auto";
      case MappingMode::PREFER_0_BASED: return "prefer-0-based";
      case MappingMode::PREFER_1_BASED: return "prefer-1-based";
      case MappingMode::STRICT_0_BASED: return "strict-0-based";
      case MappingMode::STRICT_1_BASED: return "strict-1-based";
      default: return "auto";
    }
  };
  int total = esc_count>0? esc_count : 8;

  auto direct_warmup = [&](int idx0) -> bool {
    uint16_t direct = (uint16_t)(0x0004u + (uint16_t)(idx0 & 0xFF));
    SelectionReport drep;
    auto dsig = fw.diagnostic_init_flash_direct(direct, 1, &drep);
    if (dsig.has_value()) {
      fw.reset_selected(ResetMode::ExitBootloader);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return dsig.has_value();
  };

  auto recover_index_from_direct = [&](int idx0) -> bool {
    uint16_t direct = (uint16_t)(0x0004u + (uint16_t)(idx0 & 0xFF));

    // Attempt 1: direct kick → reset → reselect by index
    {
      SelectionReport drep;
      auto dsig = fw.diagnostic_init_flash_direct(direct, 2, &drep);
      if (!dsig.has_value()) {
        log.info("recover-direct: esc=" + std::to_string(idx0 + 1) + " direct_ok=no");
        return false;
      }
      fw.reset_selected(ResetMode::ExitBootloader);
      std::this_thread::sleep_for(std::chrono::milliseconds(150));

      SelectionReport srep;
      bool sel = fw.select_target_session(idx0, mapping_mode,
                                          /*allow_alt_addresses=*/false,
                                          /*direct_attempts=*/1,
                                          /*index_attempts=*/4,
                                          &srep);
      if (sel) {
        fw.reset_selected(ResetMode::ExitBootloader);
        log.info("recover-direct: esc=" + std::to_string(idx0 + 1) +
                 " direct_ok=yes index_ok=yes attempts=" + std::to_string(srep.attempts));
        return true;
      }
      log.info("recover-direct: esc=" + std::to_string(idx0 + 1) +
               " direct_ok=yes index_ok=no (attempt 1), trying session rebuild...");
    }

    // Attempt 2: session rebuild → direct kick → reselect
    {
      if (fw.last_selection_ok()) { fw.reset_selected(ResetMode::RestartBootloader); }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!fw.test_alive()) {
        fw.exit_interface();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fw.set_mode_silabs();
        fw.clear_reset_cache();
        fw.test_alive();
      }

      SelectionReport drep2;
      auto dsig2 = fw.diagnostic_init_flash_direct(direct, 2, &drep2);
      if (dsig2.has_value()) {
        fw.reset_selected(ResetMode::ExitBootloader);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
      }

      SelectionReport srep2;
      bool sel2 = fw.select_target_session(idx0, mapping_mode,
                                           /*allow_alt_addresses=*/false,
                                           /*direct_attempts=*/1,
                                           /*index_attempts=*/4,
                                           &srep2);
      if (sel2) {
        fw.reset_selected(ResetMode::ExitBootloader);
        log.info("recover-direct: esc=" + std::to_string(idx0 + 1) +
                 " recovered after session rebuild, attempts=" + std::to_string(srep2.attempts));
        return true;
      }
      log.info("recover-direct: esc=" + std::to_string(idx0 + 1) +
               " failed after session rebuild");
      return false;
    }
  };

  if (esc_count > 0) {
    log.info("Warm-up: waking ESC bootloaders (value-based)...");
    for (int v = 0; v < esc_count; ++v) {
      SelectionReport rep;
      auto sig = fw.diagnostic_init_flash_index_val((uint8_t)v, 1, &rep);
      if (sig.has_value()) {
        fw.reset_selected(ResetMode::ExitBootloader);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        log.info("warm-up: val=" + std::to_string(v) + " ok (sig=0x" +
                 [&](){ char b[8]; snprintf(b,8,"%04X", *sig); return std::string(b); }() + ")");
      }
    }
    for (int v = 1; v <= esc_count; ++v) {
      if (v < esc_count) continue;
      SelectionReport rep;
      auto sig = fw.diagnostic_init_flash_index_val((uint8_t)v, 1, &rep);
      if (sig.has_value()) {
        fw.reset_selected(ResetMode::ExitBootloader);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        log.info("warm-up: val=" + std::to_string(v) + " ok (sig=0x" +
                 [&](){ char b[8]; snprintf(b,8,"%04X", *sig); return std::string(b); }() + ")");
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  auto sel_val_for = [&](MappingMode m, int idx0)->int{
    bool one = (m == MappingMode::PREFER_1_BASED || m == MappingMode::STRICT_1_BASED);
    return one ? (idx0+1) : idx0;
  };

  auto probe_with = [&](MappingMode m)->std::vector<bool>{
    std::vector<bool> okv(total, false);
    for (int i=0;i<total;++i){
      SelectionReport rep;
      bool ok = fw.select_target_session(i,
                                         m,
                                         /*allow_alt_addresses=*/false,
                                         /*direct_attempts=*/std::max(1, args.probe_tries),
                                         /*index_attempts=*/std::max(1, args.probe_tries),
                                         &rep);
      okv[i] = ok;
      std::string sigs = "--";
      if (rep.sig.has_value()) { char b[8]; snprintf(b,8,"%04X", *rep.sig); sigs = std::string("0x") + b; }
      log.info("probe: esc="+std::to_string(i+1)+" val="+std::to_string(sel_val_for(m,i))+" ok="+(ok?"yes":"no")+" sig="+sigs+" tries="+std::to_string(rep.attempts));
      if (ok) { fw.reset_selected(ResetMode::ExitBootloader); }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return okv;
  };

  auto score_with = [&](MappingMode m, const std::string& tag, int tries)->std::vector<bool>{
    std::vector<bool> okv(total, false);
    for (int i=0;i<total;++i){
      SelectionReport rep;
      bool ok = fw.select_target_session(i,
                                         m,
                                         /*allow_alt_addresses=*/false,
                                         /*direct_attempts=*/std::max(1, tries),
                                         /*index_attempts=*/std::max(1, tries),
                                         &rep);
      okv[i] = ok;
      std::string sigs = "--";
      if (rep.sig.has_value()) { char b[8]; snprintf(b,8,"%04X", *rep.sig); sigs = std::string("0x") + b; }
      log.info("score "+tag+": idx0="+std::to_string(i)+" val="+std::to_string(sel_val_for(m,i))+" ok="+(ok?"yes":"no")+" sig="+sigs+" tries="+std::to_string(rep.attempts));
      if (ok) { fw.reset_selected(ResetMode::ExitBootloader); }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return okv;
  };

  auto count_ok = [&](const std::vector<bool>& v){ int c=0; for (auto b: v) if (b) ++c; return c; };

  std::optional<std::vector<bool>> reachable_from_scoring;
  if (mapping_mode == MappingMode::AUTO && esc_count > 0 && (args.all || args.index > 0 || args.probe_escs || args.probe_sweep || args.read || args.write_settings)) {
    double prev_sleep = fw.get_probe_sleep();

    {
      int qtries = std::min(3, std::max(2, args.probe_tries / 2));
      double qs = std::max(args.probe_sleep, 0.05);
      fw.set_probe_sleep(std::max(prev_sleep, qs));

      auto quick_sel = [&](MappingMode m, int idx0)->bool{
        SelectionReport rep;
        bool ok = fw.select_target_session(idx0,
                                           m,
                                           /*allow_alt_addresses=*/false,
                                           /*direct_attempts=*/1,
                                           /*index_attempts=*/qtries,
                                           &rep);
        if (ok) { fw.reset_selected(ResetMode::ExitBootloader); }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return ok;
      };

      log.info("AUTO mapping phase-1: quick probes...");
      bool s0_a = quick_sel(MappingMode::STRICT_0_BASED, 0);
      bool s0_b = quick_sel(MappingMode::STRICT_0_BASED, total - 1);
      bool s1_a = quick_sel(MappingMode::STRICT_1_BASED, 0);
      bool s1_b = quick_sel(MappingMode::STRICT_1_BASED, total - 1);
      int qs0 = (s0_a ? 1 : 0) + (s0_b ? 1 : 0);
      int qs1 = (s1_a ? 1 : 0) + (s1_b ? 1 : 0);
      log.info("AUTO mapping phase-1 score: strict-0=" + std::to_string(qs0) + "/2 strict-1=" + std::to_string(qs1) + "/2");

      fw.set_probe_sleep(prev_sleep);

      // Only resolve on strong evidence (2/0 vs 0/2). Weak scores like 1/2 vs 0/2
      // can be caused by transient flakiness and would poison the rest of the run.
      if (qs0 == 2 && qs1 == 0) {
        mapping_mode = MappingMode::STRICT_0_BASED;
        log.info("AUTO mapping resolved to: " + mapping_mode_str(mapping_mode) + " (phase-1, strong)");
      } else if (qs1 == 2 && qs0 == 0) {
        mapping_mode = MappingMode::STRICT_1_BASED;
        log.info("AUTO mapping resolved to: " + mapping_mode_str(mapping_mode) + " (phase-1, strong)");
      } else if (qs0 != qs1) {
        log.info("AUTO mapping phase-1 inconclusive (weak evidence " + std::to_string(qs0) + " vs " + std::to_string(qs1) + "); continuing to full scoring");
      }
    }

    if (mapping_mode != MappingMode::AUTO) {
      // Resolved via phase-1; skip full scoring.
    } else {

    auto run_pass = [&](int tries, double sleep)->std::pair<std::vector<bool>, std::vector<bool>>{
      fw.set_probe_sleep(std::max(prev_sleep, sleep));
      auto ok0 = score_with(MappingMode::STRICT_0_BASED, "strict-0", tries);
      int s0 = count_ok(ok0);
      // score_with already resets after each successful selection; just settle
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      auto ok1 = score_with(MappingMode::STRICT_1_BASED, "strict-1", tries);
      int s1 = count_ok(ok1);
      log.info("AUTO mapping score: strict-0="+std::to_string(s0)+"/"+std::to_string(total)+" strict-1="+std::to_string(s1)+"/"+std::to_string(total));
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      fw.set_probe_sleep(prev_sleep);
      return {ok0, ok1};
    };

    int tries0 = std::max(args.probe_tries, 8);
    double sleep0 = std::max(args.probe_sleep, 0.05);

    auto pass0 = run_pass(tries0, sleep0);
    int s0 = count_ok(pass0.first);
    int s1 = count_ok(pass0.second);

    if (s0 == s1 && s0 < total) {
      int tries1 = std::max(tries0, 10);
      double sleep1 = std::max(sleep0, 0.10);
      auto pass1 = run_pass(tries1, sleep1);
      pass0 = pass1;
      s0 = count_ok(pass0.first);
      s1 = count_ok(pass0.second);
    }

    if (s0 > s1) { mapping_mode = MappingMode::STRICT_0_BASED; reachable_from_scoring = pass0.first; }
    else if (s1 > s0) { mapping_mode = MappingMode::STRICT_1_BASED; reachable_from_scoring = pass0.second; }
    else {
      bool is_read_like = args.read || args.write_settings || args.probe_escs || args.probe_sweep;
      if (is_read_like) {
        // For read-like ops, don't abort - fall back to STRICT_0_BASED best-effort
        mapping_mode = MappingMode::STRICT_0_BASED;
        log.info("AUTO mapping ambiguous - falling back to strict-0-based for read operation");
      } else {
        log.info("AUTO mapping ambiguous under current link quality; use --mapping strict-0-based/strict-1-based or increase --probe-tries/--probe-sleep");
        return exit_and_return(1);
      }
    }

    if (mapping_mode == MappingMode::STRICT_0_BASED && s0 == total && s1 == total - 1) {
      bool strict1_only_last_fails = true;
      for (int i=0; i<total-1; ++i) if (!pass0.second[i]) { strict1_only_last_fails = false; break; }
      if (strict1_only_last_fails && !pass0.second[total-1]) {
        log.info("Hint: strict-1 fails only at val="+std::to_string(total)+" while val=0.."+std::to_string(total-1)+" succeed -> strongly suggests 0-based indexing; ESC"+std::to_string(total)+" corresponds to val="+std::to_string(total-1)+", not val="+std::to_string(total)+".");
      }
    }

    if (mapping_mode != MappingMode::AUTO) {
      log.info("AUTO mapping resolved to: " + mapping_mode_str(mapping_mode));
    }
    }
  }

  bool mapping_ambiguous = (mapping_mode == MappingMode::AUTO);
  {
    std::ostringstream oss;
    oss << "{\"type\":\"mapping_resolved\",\"mode\":\"" << json_escape(mapping_mode_str(mapping_mode)) << "\"";
    if (esc_count > 0) oss << ",\"esc_count\":" << esc_count;
    if (mapping_ambiguous) oss << ",\"ambiguous\":true";
    oss << "}";
    ndjson_emit(args.ui_json, oss.str());
  }

  log.info("Mapping mode: " + mapping_mode_str(mapping_mode) + (mapping_mode==MappingMode::AUTO? " (will try both)" : ""));
  fl.mapping_mode = mapping_mode;

  SettingsMode settings_mode = SettingsMode::PRESERVE;
  if (args.settings == "preserve") settings_mode = SettingsMode::PRESERVE;
  else if (args.settings == "erase") settings_mode = SettingsMode::ERASE;
  else if (args.settings == "migrate") settings_mode = SettingsMode::MIGRATE;
  if (args.erase_eeprom) settings_mode = SettingsMode::ERASE;
  fl.settings_mode = settings_mode;

  auto settings_mode_str = [&](SettingsMode s)->std::string{
    switch(s){
      case SettingsMode::PRESERVE: return "preserve";
      case SettingsMode::ERASE: return "erase";
      case SettingsMode::MIGRATE: return "migrate";
      default: return "preserve";
    }
  };
  log.info("Settings mode: " + settings_mode_str(settings_mode));

  std::vector<bool> reachable;
  if (esc_count > 0 && (args.all || args.probe_escs || args.probe_sweep || (args.read && args.all) || (args.write_settings && args.all))) {
    if (reachable_from_scoring.has_value()) {
      reachable = *reachable_from_scoring;
    } else if (mapping_mode == MappingMode::AUTO) {
      // Try both STRICT0 and STRICT1, mark reachable if either works
      auto ok0 = probe_with(MappingMode::STRICT_0_BASED);
      auto ok1 = probe_with(MappingMode::STRICT_1_BASED);
      reachable.resize(total, false);
      for (int i = 0; i < total; ++i) reachable[i] = ok0[i] || ok1[i];
    } else {
      reachable = probe_with(mapping_mode);
    }
    // Emit per-ESC reachability events for GUI
    for (int i = 0; i < total; ++i) {
      ndjson_emit(args.ui_json,
                  std::string("{\"type\":\"esc_reachability\",\"index\":") + std::to_string(i) +
                  ",\"reachable\":" + (reachable[i] ? "true" : "false") + "}");
    }

    int okc = count_ok(reachable);
    if (okc != total) {
      std::string miss;
      for (int i=0;i<total;++i) if (!reachable[i]) { if(!miss.empty()) miss.push_back(','); miss += std::to_string(i+1); }
      log.info("Reachability: "+std::to_string(okc)+"/"+std::to_string(total)+" reachable via index select; missing: "+miss);

      for (int i=0;i<total;++i) {
        if (reachable[i]) continue;
        log.info("Attempting direct-kick recovery for ESC" + std::to_string(i+1) + "...");
        bool recovered = recover_index_from_direct(i);
        if (recovered) {
          reachable[i] = true;
          log.info("Recovered ESC" + std::to_string(i+1) + " via direct kick");
          ndjson_emit(args.ui_json,
                      std::string("{\"type\":\"esc_reachability\",\"index\":") + std::to_string(i) +
                      ",\"reachable\":true}");
        } else {
          log.info("ESC" + std::to_string(i+1) + " unrecoverable (direct kick failed)");
        }
      }

      int okc2 = count_ok(reachable);
      if (okc2 > okc) {
        log.info("Reachability after direct-kick recovery: " + std::to_string(okc2) + "/" + std::to_string(total));
      }

      if (okc2 != total) {
        // Only abort for flash/write ops; reads should continue with partial results
        bool is_read_op = args.read || args.write_settings;
        if (args.all && !args.skip_missing && !is_read_op) {
          std::string miss2;
          for (int i=0;i<total;++i) if (!reachable[i]) { if(!miss2.empty()) miss2.push_back(','); miss2 += std::to_string(i+1); }
          log.info("Aborting --all (still missing ESCs: " + miss2 + "). Use --skip-missing to flash only reachable ESCs.");
          return exit_and_return(1);
        }
        if (is_read_op) {
          log.info("Continuing with reachable ESCs only (read/write-settings mode).");
        }
      }
    }
  }

  if ((args.probe_escs || args.probe_sweep) && esc_count > 0 && args.probe_sweep) {
    int maxv = std::min(255, esc_count + 2);
    for (int v=0; v<=maxv; ++v) {
      SelectionReport srep;
      auto ssig = fw.diagnostic_init_flash_index_val((uint8_t)v, std::max(1, args.probe_tries), &srep);
      std::string sigs = "--";
      if (ssig.has_value()) { char b[8]; snprintf(b,8,"%04X", *ssig); sigs = std::string("0x") + b; }
      log.info("probe-sweep: val="+std::to_string(v)+" ok="+(ssig.has_value()?"yes":"no")+" sig="+sigs+" tries="+std::to_string(srep.attempts));
      if (ssig.has_value()) { fw.reset_selected(ResetMode::ExitBootloader); }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  if (args.probe_escs) {
    return exit_and_return(0);
  }

  if (args.read) {
    int start_idx = 0;
    int end_idx = total;
    if (!args.all && args.index > 0) {
      start_idx = std::max(0, args.index - 1);
      end_idx = std::min(total, start_idx + 1);
    }

    bool read_any_fail = false;
    bool read_any_ok = false;

    auto print_row = [&](int i, const ReadRowFull& rr){
      std::string sigs = "--";
      if (rr.sig.has_value()) { char b[8]; snprintf(b, 8, "%04X", *rr.sig); sigs = std::string("0x") + b; }

      const auto& id = rr.identity;
      const auto& d = rr.display;

      std::string ver = "v" + std::to_string(id.fw_main) + "." + std::to_string(id.fw_sub);
      std::string pwm_str = id.pwm_khz == 0 ? "Dynamic" : (std::to_string(id.pwm_khz) + "kHz");

      std::string note;
      if (!rr.mapping_used.empty()) note = "mapping " + rr.mapping_used;

      log.info("ESC" + std::to_string(i + 1) + ": Bluejay " + ver + " (layout " + std::to_string(id.layout_version) + "), " +
               "Target=" + (id.layout_name.empty() ? "--" : id.layout_name) + ", PWM=" + pwm_str + " [sig=" + sigs + "]" +
               (note.empty() ? "" : " - " + note));

      log.info("  Settings:");
      log.info("    Startup power min (boost): " + std::to_string(d.startup_power_min));
      log.info("    Startup power max (protection): " + std::to_string(d.startup_power_max));
      log.info("    Motor timing: " + d.commutation_timing);
      log.info("    Demag compensation: " + d.demag_compensation);
      log.info("    RPM power protection: " + d.rpm_power_slope);
      log.info("    Beep strength: " + std::to_string(d.beep_strength));
      log.info("    Beacon strength: " + std::to_string(d.beacon_strength));
      log.info("    Beacon delay: " + d.beacon_delay);
      log.info("    ESC power rating: " + d.power_rating);
      log.info("    Temperature protection: " + d.temperature_protection);
      log.info("    Force EDT arm: " + d.force_edt_arm);
      log.info("    Brake on stop: " + d.brake_on_stop);
      log.info("    Braking strength: " + std::to_string(d.braking_strength));
      log.info("    Motor direction: " + d.motor_direction);
      if (id.layout_version >= 209 && id.pwm_khz == 0) {
        log.info("    Threshold 96->48: " + std::to_string(d.threshold_96to48));
        log.info("    Threshold 48->24: " + std::to_string(d.threshold_48to24));
      }

      if (args.trace) {
        const auto& s = rr.settings;
        log.info("[TRACE] eeprom layout_version=" + std::to_string(id.layout_version) + " fw=" + std::to_string(id.fw_main) + "." + std::to_string(id.fw_sub));
        log.info("[TRACE] STARTUP_POWER_MIN raw=" + std::to_string(s.startup_power_min) + " display=" + std::to_string(d.startup_power_min));
        log.info("[TRACE] STARTUP_POWER_MAX raw=" + std::to_string(s.startup_power_max) + " display=" + std::to_string(d.startup_power_max));
        log.info("[TRACE] COMMUTATION_TIMING raw=" + std::to_string(s.commutation_timing));
        log.info("[TRACE] DEMAG_COMPENSATION raw=" + std::to_string(s.demag_compensation));
        log.info("[TRACE] RPM_POWER_SLOPE raw=" + std::to_string(s.rpm_power_slope));
        log.info("[TRACE] BEEP_STRENGTH raw=" + std::to_string(s.beep_strength));
        log.info("[TRACE] BEACON_STRENGTH raw=" + std::to_string(s.beacon_strength));
        log.info("[TRACE] BEACON_DELAY raw=" + std::to_string(s.beacon_delay));
        log.info("[TRACE] POWER_RATING raw=" + std::to_string(s.power_rating));
        log.info("[TRACE] TEMPERATURE_PROTECTION raw=" + std::to_string(s.temperature_protection));
        log.info("[TRACE] FORCE_EDT_ARM raw=" + std::to_string(s.force_edt_arm));
        log.info("[TRACE] BRAKE_ON_STOP raw=" + std::to_string(s.brake_on_stop));
        log.info("[TRACE] BRAKING_STRENGTH raw=" + std::to_string(s.braking_strength));
        log.info("[TRACE] MOTOR_DIRECTION raw=" + std::to_string(s.motor_direction));
      }
    };

    auto emit_settings = [&](int i, const ReadRowFull& rr) {
      std::ostringstream oss;

      std::string sigs;
      if (rr.sig.has_value()) { char b[8]; std::snprintf(b, 8, "%04X", *rr.sig); sigs = b; }

      const auto& id = rr.identity;
      const auto& s = rr.settings;

      oss << "{\"type\":\"esc_settings\",\"esc_index\":" << (i + 1);
      if (!sigs.empty()) oss << ",\"sig\":\"0x" << sigs << "\"";
      oss << ",\"identity\":{";
      oss << "\"layout_version\":" << (int)id.layout_version;
      oss << ",\"fw\":\"" << (int)id.fw_main << "." << (int)id.fw_sub << "\"";
      oss << ",\"target\":\"" << json_escape(id.layout_name) << "\"";
      oss << ",\"pwm_khz\":" << id.pwm_khz;
      oss << "}";

      oss << ",\"settings\":{";
      oss << "\"STARTUP_POWER_MIN\":" << (int)bj_startup_power_min_to_display(s.startup_power_min);
      oss << ",\"STARTUP_POWER_MAX\":" << (int)bj_startup_power_max_to_display(s.startup_power_max);
      oss << ",\"COMMUTATION_TIMING\":" << (int)s.commutation_timing;
      oss << ",\"DEMAG_COMPENSATION\":" << (int)s.demag_compensation;
      oss << ",\"RPM_POWER_SLOPE\":" << (int)s.rpm_power_slope;
      oss << ",\"BEEP_STRENGTH\":" << (int)s.beep_strength;
      oss << ",\"BEACON_STRENGTH\":" << (int)s.beacon_strength;
      oss << ",\"BEACON_DELAY\":" << (int)s.beacon_delay;
      oss << ",\"POWER_RATING\":" << (int)s.power_rating;
      oss << ",\"TEMPERATURE_PROTECTION\":" << (int)s.temperature_protection;
      oss << ",\"FORCE_EDT_ARM\":" << (int)s.force_edt_arm;
      oss << ",\"BRAKE_ON_STOP\":" << (int)s.brake_on_stop;
      oss << ",\"BRAKING_STRENGTH\":" << (int)s.braking_strength;
      oss << ",\"MOTOR_DIRECTION\":" << (int)s.motor_direction;
      oss << ",\"LED_CONTROL\":" << (int)s.led_control;
      oss << ",\"STARTUP_BEEP\":" << (int)s.startup_beep;
      oss << ",\"DITHERING\":" << (int)s.dithering;
      oss << ",\"PWM_FREQUENCY\":" << (int)s.pwm_frequency;
      oss << ",\"THRESHOLD_96TO48\":" << (int)s.threshold_96to48;
      oss << ",\"THRESHOLD_48TO24\":" << (int)s.threshold_48to24;
      oss << "}";

      if (!rr.mapping_used.empty()) {
        oss << ",\"mapping_used\":\"" << json_escape(rr.mapping_used) << "\"";
      }

      oss << "}";
      ndjson_emit(args.ui_json, oss.str());
    };

    if (args.all && args.index < 0) {
      int rounds = std::max(1, args.read_rounds);
      int base_sleep = std::max(0, args.read_round_sleep_ms);

      std::vector<int> success_rounds(total, 0);
      std::vector<int> attempted_rounds(total, 0);

      std::vector<int> pending;
      pending.reserve(end_idx - start_idx);
      for (int i = start_idx; i < end_idx; ++i) {
        if (!reachable.empty() && !reachable[i]) {
          log.info("ESC" + std::to_string(i + 1) + ": read skipped (unreachable)");
          ndjson_emit(args.ui_json,
                      std::string("{\"type\":\"esc_read_summary\",\"esc_index\":") + std::to_string(i + 1) +
                      ",\"success_rounds\":0,\"total_rounds\":" + std::to_string(rounds) +
                      ",\"stable\":false,\"skipped\":true}");
          continue;
        }
        pending.push_back(i);
      }

      for (int round = 1; round <= rounds && !pending.empty(); ++round) {
        std::vector<int> next_pending;
        for (int i : pending) {
          attempted_rounds[i]++;
          ndjson_emit(args.ui_json,
                      std::string("{\"type\":\"esc_read_start\",\"esc_index\":") +
                      std::to_string(i + 1) + ",\"round\":" + std::to_string(round) + "}");
          ReadRowFull rr;
          bool ok = fl.read_settings_full(i, &rr, /*allow_alt_addressing=*/false);
          if (ok && rr.ok) {
            if (!rr.mapping_used.empty() && rr.mapping_used == "direct") {
              ok = false;
              rr.ok = false;
              rr.error = "read used direct addressing (unreliable in multi-ESC mode)";
            }
          }
          if (ok && rr.ok) {
            success_rounds[i]++;
            print_row(i, rr);
            read_any_ok = true;
            ndjson_emit(args.ui_json,
                        std::string("{\"type\":\"esc_read_ok\",\"esc_index\":") + std::to_string(i + 1) +
                        ",\"round\":" + std::to_string(round) + "}");
            emit_settings(i, rr);
          } else {
            ndjson_emit(args.ui_json,
                        std::string("{\"type\":\"esc_read_fail\",\"esc_index\":") + std::to_string(i + 1) +
                        ",\"round\":" + std::to_string(round) + ",\"error\":\"" + json_escape(rr.error) + "\"}");
            {
              std::ostringstream diag;
              diag << "{\"type\":\"esc_select_fail\",\"esc_index\":" << (i + 1)
                   << ",\"mapping_mode\":\"" << json_escape(mapping_mode_str(mapping_mode)) << "\""
                   << ",\"attempts\":" << rr.select_attempts
                   << ",\"error\":\"" << json_escape(rr.error) << "\"";
              if (!rr.mapping_used.empty()) diag << ",\"mapping_used\":\"" << json_escape(rr.mapping_used) << "\"";
              diag << "}";
              ndjson_emit(args.ui_json, diag.str());
            }
            std::string note = rr.error.empty() ? "read failed" : rr.error;
            if (!rr.mapping_used.empty()) note += ", mapping " + rr.mapping_used;

            std::string sigs = "--";
            if (rr.sig.has_value()) { char b[8]; snprintf(b, 8, "%04X", *rr.sig); sigs = std::string("0x") + b; }

            if (round < rounds) {
              log.info("ESC" + std::to_string(i + 1) + ": settings unavailable [sig=" + sigs + "] - " + note +
                       " - will retry (round " + std::to_string(round + 1) + "/" + std::to_string(rounds) + ")");
            } else {
              log.info("ESC" + std::to_string(i + 1) + ": settings unavailable [sig=" + sigs + "] - " + note +
                       " - giving up after " + std::to_string(rounds) + " rounds");
            }
            next_pending.push_back(i);

            if (fw.last_selection_ok()) { fw.reset_selected(ResetMode::ExitBootloader); }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!fw.test_alive()) {
              log.info("4-way interface lost after ESC" + std::to_string(i + 1) + " failure; recovering...");
              fl.bringup_4way();
              std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
          }

          if (fw.last_selection_ok()) { fw.reset_selected(ResetMode::ExitBootloader); }
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        pending = std::move(next_pending);
        if (!pending.empty() && round < rounds && base_sleep > 0) {
          int sleep_ms = base_sleep;
          for (int k = 1; k < round; ++k) sleep_ms = std::min(1500, sleep_ms * 2);
          std::string miss;
          for (int j = 0; j < (int)pending.size(); ++j) {
            if (j) miss.push_back(',');
            miss += std::to_string(pending[j] + 1);
          }
          log.info("Retrying failed ESCs: " + miss + " (round " + std::to_string(round + 1) + "/" + std::to_string(rounds) + ") after " + std::to_string(sleep_ms) + "ms...");
          std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        }
      }

      for (int i = start_idx; i < end_idx; ++i) {
        if (!reachable.empty() && !reachable[i]) continue;  // already emitted above
        bool stable = (success_rounds[i] == attempted_rounds[i]) && (success_rounds[i] > 0);
        ndjson_emit(args.ui_json,
                    std::string("{\"type\":\"esc_read_summary\",\"esc_index\":") + std::to_string(i + 1) +
                    ",\"success_rounds\":" + std::to_string(success_rounds[i]) +
                    ",\"total_rounds\":" + std::to_string(attempted_rounds[i]) +
                    ",\"stable\":" + (stable ? "true" : "false") + "}");
      }

      read_any_fail = !pending.empty();
    } else {
      for (int i = start_idx; i < end_idx; ++i) {
        if (!reachable.empty() && !reachable[i]) {
          log.info("ESC" + std::to_string(i + 1) + ": read skipped (unreachable)");
          continue;
        }

        ReadRowFull rr;
        ndjson_emit(args.ui_json,
                    std::string("{\"type\":\"esc_read_start\",\"esc_index\":") + std::to_string(i + 1) + "}");
        bool ok = fl.read_settings_full(i, &rr, /*allow_alt_addressing=*/true);
        std::string sigs = "--";
        if (rr.sig.has_value()) { char b[8]; snprintf(b, 8, "%04X", *rr.sig); sigs = std::string("0x") + b; }
        if (ok && rr.ok && !rr.mapping_used.empty() && rr.mapping_used == "direct") {
          ndjson_emit(args.ui_json,
                      std::string("{\"type\":\"esc_read_degraded\",\"esc_index\":") + std::to_string(i + 1) +
                      ",\"used_direct\":true}");
          log.info("ESC" + std::to_string(i + 1) + ": WARNING - read used direct addressing (identity not guaranteed)");
        }
        if (!ok || !rr.ok) {
          read_any_fail = true;
          ndjson_emit(args.ui_json,
                      std::string("{\"type\":\"esc_read_fail\",\"esc_index\":") + std::to_string(i + 1) +
                      ",\"error\":\"" + json_escape(rr.error) + "\"}");
          std::string note = rr.error.empty() ? "read failed" : rr.error;
          if (!rr.mapping_used.empty()) note += ", mapping " + rr.mapping_used;
          log.info("ESC" + std::to_string(i + 1) + ": settings unavailable [sig=" + sigs + "] - " + note);
          if (fw.last_selection_ok()) { fw.reset_selected(ResetMode::ExitBootloader); }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          if (!fw.test_alive()) {
            log.info("4-way interface lost after ESC" + std::to_string(i + 1) + " failure; recovering...");
            fl.bringup_4way();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          }
          continue;
        }

        print_row(i, rr);
        read_any_ok = true;
        ndjson_emit(args.ui_json,
                    std::string("{\"type\":\"esc_read_ok\",\"esc_index\":") + std::to_string(i + 1) + "}");
        emit_settings(i, rr);
        fw.reset_selected(ResetMode::ExitBootloader);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }

    bool ui_success = read_any_ok && !read_any_fail;
    ndjson_emit(args.ui_json,
                std::string("{\"type\":\"op_done\",\"op\":\"read\",\"success\":") + (ui_success ? "true" : "false") + "}");
    return exit_and_return(0);
  }

  // --write-settings mode
  if (args.write_settings) {
    if (args.set_args.empty()) {
      log.info("--write-settings requires at least one --set KEY=VALUE argument");
      return exit_and_return(2);
    }

    int start_idx = 0;
    int end_idx = total;
    if (!args.all && args.index > 0) {
      start_idx = std::max(0, args.index - 1);
      end_idx = std::min(total, start_idx + 1);
    }

    bool all_ok = true;
    for (int i = start_idx; i < end_idx; ++i) {
      if (!reachable.empty() && !reachable[i]) {
        log.info("ESC" + std::to_string(i + 1) + ": write skipped (unreachable)");
        all_ok = false;
        continue;
      }

      BluejaySettingsDisplay after;
      ndjson_emit(args.ui_json,
                  std::string("{\"type\":\"esc_write_start\",\"esc_index\":") + std::to_string(i + 1) + "}");
      bool ok = fl.update_settings(i, settings_patch, &after);
      if (!ok) {
        log.info("ESC" + std::to_string(i + 1) + ": settings update FAILED");
        ndjson_emit(args.ui_json,
                    std::string("{\"type\":\"esc_write_fail\",\"esc_index\":") + std::to_string(i + 1) + "}");
        all_ok = false;
        continue;
      }

      ndjson_emit(args.ui_json,
                  std::string("{\"type\":\"esc_write_ok\",\"esc_index\":") + std::to_string(i + 1) + "}");

      log.info("ESC" + std::to_string(i + 1) + ": settings updated successfully");
      log.info("  Updated settings:");
      log.info("    Startup power min (boost): " + std::to_string(after.startup_power_min));
      log.info("    Startup power max (protection): " + std::to_string(after.startup_power_max));
      log.info("    Motor timing: " + after.commutation_timing);
      log.info("    Demag compensation: " + after.demag_compensation);
      log.info("    RPM power protection: " + after.rpm_power_slope);
      log.info("    Brake on stop: " + after.brake_on_stop);
      log.info("    Braking strength: " + std::to_string(after.braking_strength));
    }

    log.info(all_ok ? "All settings updated." : "Finished with errors.");
    ndjson_emit(args.ui_json,
                std::string("{\"type\":\"op_done\",\"op\":\"write_settings\",\"success\":") + (all_ok ? "true" : "false") + "}");
    return exit_and_return(all_ok ? 0 : 1);
  }

  // --hex auto: resolve bundled Bluejay hex from ESC1 identity
  if (args.hex == "auto") {
    log.info("Bluejay auto-hex: reading ESC1 identity to resolve firmware...");
    int ref_idx = (args.index > 0) ? (args.index - 1) : 0;
    ReadRowFull rr;
    bool read_ok = fl.read_settings_full(ref_idx, &rr, /*allow_alt_addressing=*/true);
    fw.reset_selected(ResetMode::ExitBootloader);
    if (!read_ok) {
      log.info("Bluejay auto-hex: could not read ESC" + std::to_string(ref_idx + 1) + " identity");
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"flash\",\"success\":false}");
      return exit_and_return(1);
    }
    std::string slug = normalize_target_slug(rr.identity.layout_name);
    int pwm = rr.identity.pwm_khz;
    log.info("Bluejay auto-hex: ESC" + std::to_string(ref_idx + 1) + " target=" + slug + " pwm=" + std::to_string(pwm));

    std::string exe_dir;
#ifdef _WIN32
    char path_buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path_buf, MAX_PATH);
    if (len > 0) {
      exe_dir = std::string(path_buf);
      size_t last_sep = exe_dir.find_last_of("\\/");
      if (last_sep != std::string::npos) exe_dir = exe_dir.substr(0, last_sep);
    }
#endif
    args.hex = resolve_bundled_bluejay_hex(exe_dir, args.bluejay_dir, slug, pwm, args.bluejay_version, log);
    if (args.hex.empty()) {
      log.info("Bluejay auto-hex: firmware not found for target=" + slug + " pwm=" + std::to_string(pwm) + " version=" + args.bluejay_version);
      ndjson_emit(args.ui_json, "{\"type\":\"op_done\",\"op\":\"flash\",\"success\":false}");
      return exit_and_return(1);
    }
    log.info("Bluejay auto-hex: resolved to " + args.hex);
  }

  bool ok_all = true;
  auto parse_sig = [&](const std::string& s)->std::optional<uint16_t>{ if(s.empty()) return std::nullopt; unsigned val=0; if (s.rfind("0x",0)==0 || s.rfind("0X",0)==0) val = std::stoul(s, nullptr, 16); else val = std::stoul(s); return (uint16_t)val; };

  bool have_settings_patch = !args.set_args.empty();

  auto verify_mode_str = [&](VerifyMode m)->std::string{
    switch (m) {
      case VerifyMode::FAST: return "fast";
      case VerifyMode::FULL: return "full";
      case VerifyMode::NONE: return "off";
      default: return "off";
    }
  };

  {
    std::ostringstream oss;
    oss << "{\"type\":\"flash_plan\"";
    oss << ",\"hex_path\":\"" << json_escape(args.hex) << "\"";
    oss << ",\"hex_name\":\"" << json_escape(path_basename(args.hex)) << "\"";
    oss << ",\"verify\":\"" << json_escape(verify_mode_str(args.verify_mode)) << "\"";
    if (args.all) {
      oss << ",\"targets\":\"all\"";
    } else {
      oss << ",\"targets\":\"index\"";
      oss << ",\"index\":" << std::max(0, args.index - 1);
    }
    oss << ",\"opts\":{";
    oss << "\"erase_eeprom\":" << json_bool(args.erase_eeprom);
    oss << ",\"full_erase_app\":" << json_bool(args.full_erase_app);
    oss << ",\"full_erase_entire_app\":" << json_bool(args.full_erase_entire_app);
    oss << ",\"verify_all_bytes\":" << json_bool(args.verify_all_bytes);
    oss << ",\"dry_run\":" << json_bool(args.dry_run);
    if (!args.assume_sig.empty()) oss << ",\"assume_sig\":\"" << json_escape(args.assume_sig) << "\"";
    oss << "}}";
    ndjson_emit(args.ui_json, oss.str());
  }

  auto preselect_warmup = [&](int esc_idx) -> bool {
    if (args.flash_preselect_tries > 0) {
      for (int pt = 0; pt < args.flash_preselect_tries; ++pt) {
        SelectionReport wrep;
        bool wok = fw.select_target_session(esc_idx, mapping_mode,
                                            /*allow_alt_addresses=*/false,
                                            /*direct_attempts=*/1,
                                            /*index_attempts=*/2,
                                            &wrep);
        if (wok) {
          fw.reset_selected(ResetMode::ExitBootloader);
          std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, args.flash_post_select_ms)));
          return true;
        }
        if (pt < args.flash_preselect_tries - 1) {
          std::this_thread::sleep_for(std::chrono::milliseconds(args.flash_preselect_sleep_ms));
        }
      }
      log.info("Preselect index-select failed for ESC" + std::to_string(esc_idx + 1) + "; attempting direct-kick recovery...");
      bool recovered = recover_index_from_direct(esc_idx);
      std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, args.flash_post_select_ms)));
      if (recovered) {
        log.info("Preselect: ESC" + std::to_string(esc_idx + 1) + " recovered via direct kick");
        return true;
      }
      log.info("Preselect: ESC" + std::to_string(esc_idx + 1) + " still unreachable after direct-kick recovery");
      return false;
    }
    return true; // no preselect configured, assume ok
  };

  if (args.all){
    for (int i=0;i<total;++i){
      if (!reachable.empty() && !reachable[i]) {
        log.info("ESC" + std::to_string(i+1) + " was unreachable; attempting last-chance direct-kick recovery...");
        bool last_chance = recover_index_from_direct(i);
        if (!last_chance) {
          log.info("Skipping ESC"+std::to_string(i+1)+" (unreachable, direct-kick failed)");
          ndjson_emit(args.ui_json, std::string("{\"type\":\"esc_flash_skipped\",\"index\":") + std::to_string(i) + ",\"reason\":\"unreachable\"}");
          if (!args.skip_missing) ok_all = false;
          continue;
        }
        log.info("ESC" + std::to_string(i+1) + " recovered via last-chance direct kick; proceeding to flash");
        reachable[i] = true;
      }

      if (i > 0 && args.flash_inter_esc_ms > 0) {
        log.info("Inter-ESC settle: " + std::to_string(args.flash_inter_esc_ms) + "ms...");
        std::this_thread::sleep_for(std::chrono::milliseconds(args.flash_inter_esc_ms));
      }

      preselect_warmup(i);

      ndjson_emit(args.ui_json, std::string("{\"type\":\"esc_flash_start\",\"index\":") + std::to_string(i) + "}");
      bool ok = fl.flash_one(i, args.hex, args.verify_mode, args.erase_eeprom, parse_sig(args.assume_sig));
      bool esc_ok = ok;

      if (ok && have_settings_patch) {
        log.info("Applying settings overrides to ESC" + std::to_string(i + 1) + "...");
        BluejaySettingsDisplay after;
        bool sok = fl.update_settings(i, settings_patch, &after);
        if (sok) {
          log.info("ESC" + std::to_string(i + 1) + ": settings overrides applied");
        } else {
          log.info("ESC" + std::to_string(i + 1) + ": settings overrides FAILED");
          ndjson_emit(args.ui_json,
                      std::string("{\"type\":\"esc_flash_fail\",\"index\":") + std::to_string(i) +
                          ",\"error\":\"settings overrides failed\"}");
          esc_ok = false;
        }
      }

      ok_all = ok_all && esc_ok;

      if (esc_ok) {
        ndjson_emit(args.ui_json, std::string("{\"type\":\"esc_flash_ok\",\"index\":") + std::to_string(i) + "}");
        fw.reset_selected(ResetMode::ExitBootloader);
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(250, args.flash_post_select_ms)));
        if (!fw.test_alive()) {
          log.info("4-way interface lost after ESC" + std::to_string(i + 1) + " flash; recovering...");
          fw.exit_interface();
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          fw.set_mode_silabs();
          fw.clear_reset_cache();
          fw.test_alive();
        }
        if (i < total - 1) {
          for (int v = 0; v < esc_count; ++v) {
            SelectionReport vr;
            auto vs = fw.diagnostic_init_flash_index_val((uint8_t)v, 1, &vr);
            if (vs.has_value()) { fw.reset_selected(ResetMode::ExitBootloader); }
          }
          if (esc_count > 0) {
            SelectionReport vr;
            auto vs = fw.diagnostic_init_flash_index_val((uint8_t)esc_count, 1, &vr);
            if (vs.has_value()) { fw.reset_selected(ResetMode::ExitBootloader); }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      } else {
        if (!ok) {
          std::string reason = fl.last_error_.empty() ? "flash failed" : fl.last_error_;
          ndjson_emit(args.ui_json,
                      std::string("{\"type\":\"esc_flash_fail\",\"index\":") + std::to_string(i) +
                          ",\"error\":\"" + json_escape(reason) + "\"}");
        }
        if (fw.last_selection_ok()) { fw.reset_selected(ResetMode::ExitBootloader); }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!fw.test_alive()) {
          log.info("4-way interface lost after ESC" + std::to_string(i + 1) + " flash failure; recovering...");
          fl.bringup_4way();
          fw.clear_reset_cache();
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
    }
  } else {
    int idx0 = std::max(0, args.index-1);

    preselect_warmup(idx0);

    ndjson_emit(args.ui_json, std::string("{\"type\":\"esc_flash_start\",\"index\":") + std::to_string(idx0) + "}");
    bool ok = fl.flash_one(idx0, args.hex, args.verify_mode, args.erase_eeprom, parse_sig(args.assume_sig));
    bool esc_ok = ok;

    if (ok && have_settings_patch) {
      log.info("Applying settings overrides to ESC" + std::to_string(idx0 + 1) + "...");
      BluejaySettingsDisplay after;
      bool sok = fl.update_settings(idx0, settings_patch, &after);
      if (sok) {
        log.info("ESC" + std::to_string(idx0 + 1) + ": settings overrides applied");
      } else {
        log.info("ESC" + std::to_string(idx0 + 1) + ": settings overrides FAILED");
        ndjson_emit(args.ui_json,
                    std::string("{\"type\":\"esc_flash_fail\",\"index\":") + std::to_string(idx0) +
                        ",\"error\":\"settings overrides failed\"}");
        esc_ok = false;
      }
    }

    ok_all = ok_all && esc_ok;

    if (esc_ok) {
      ndjson_emit(args.ui_json, std::string("{\"type\":\"esc_flash_ok\",\"index\":") + std::to_string(idx0) + "}");
    } else {
      if (!ok) {
        std::string reason = fl.last_error_.empty() ? "flash failed" : fl.last_error_;
        ndjson_emit(args.ui_json,
                    std::string("{\"type\":\"esc_flash_fail\",\"index\":") + std::to_string(idx0) +
                        ",\"error\":\"" + json_escape(reason) + "\"}");
      }
    }
  }

  auto restore = exit_passthrough();

  log.info(ok_all? "All done." : "Finished with errors.");
  if (!restore.ok) {
    log.info("WARNING: MSP not restored after flash; next run may fail without power cycle.");
  }
  ndjson_emit(args.ui_json,
              std::string("{\"type\":\"op_done\",\"op\":\"flash\",\"success\":") +
                  (ok_all ? "true" : "false") + "}");
  return ok_all? 0 : 1;
}
