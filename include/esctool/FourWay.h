#pragma once
#include "esctool/ISerial.h"
#include "esctool/Log.h"
#include <vector>
#include <cstdint>
#include <optional>
#include <utility>
#include <string>

namespace esctool {
// 4-way command IDs
constexpr uint8_t CMD_InterfaceTestAlive   = 0x30;
constexpr uint8_t CMD_ProtocolGetVersion   = 0x31;
constexpr uint8_t CMD_InterfaceGetName     = 0x32;
constexpr uint8_t CMD_InterfaceGetVersion  = 0x33;
constexpr uint8_t CMD_InterfaceExit        = 0x34;
constexpr uint8_t CMD_DeviceReset          = 0x35;
constexpr uint8_t CMD_DeviceInitFlash      = 0x37;
constexpr uint8_t CMD_DeviceEraseAll       = 0x38;
constexpr uint8_t CMD_DevicePageErase      = 0x39;
constexpr uint8_t CMD_DeviceRead           = 0x3A;
constexpr uint8_t CMD_DeviceWrite          = 0x3B;
constexpr uint8_t CMD_DeviceReadEE         = 0x3D;
constexpr uint8_t CMD_DeviceWriteEE        = 0x3E;
constexpr uint8_t CMD_InterfaceSetMode     = 0x3F;
constexpr uint8_t ACK_OK = 0x00;
constexpr uint8_t ACK_I_UNKNOWN_ERROR    = 0x01;
constexpr uint8_t ACK_I_INVALID_CMD      = 0x02;
constexpr uint8_t ACK_I_INVALID_CRC      = 0x03;
constexpr uint8_t ACK_I_VERIFY_ERROR     = 0x04;
constexpr uint8_t ACK_D_INVALID_CHANNEL  = 0x08;
constexpr uint8_t ACK_I_INVALID_PARAM    = 0x09;
constexpr uint8_t ACK_D_GENERAL_ERROR    = 0x0F;

inline const char* ack_name(uint8_t ack) {
  switch (ack) {
    case ACK_OK:                return "OK";
    case ACK_I_UNKNOWN_ERROR:   return "UNKNOWN_ERROR";
    case ACK_I_INVALID_CMD:     return "INVALID_CMD";
    case ACK_I_INVALID_CRC:     return "INVALID_CRC";
    case ACK_I_VERIFY_ERROR:    return "VERIFY_ERROR";
    case ACK_D_INVALID_CHANNEL: return "INVALID_CHANNEL";
    case ACK_I_INVALID_PARAM:   return "INVALID_PARAM";
    case ACK_D_GENERAL_ERROR:   return "GENERAL_ERROR";
    default:                    return "?";
  }
}

enum class ResetMode : uint8_t { RestartBootloader = 0, ExitBootloader = 1 };

enum class MappingMode {
  AUTO=0,
  PREFER_0_BASED=1,
  PREFER_1_BASED=2,
  STRICT_0_BASED=3,
  STRICT_1_BASED=4
};

struct SelectionReport {
  bool ok{false};
  std::optional<uint16_t> sig;
  bool used_direct{false};
  uint16_t direct_addr{0};
  bool used_index{false};
  uint8_t index_val{0};
  std::string index_mapping;
  int attempts{0};
};

class FourWay {
  ISerial& ser_; Log& log_; double timeout_s_; bool trace_; double inter_delay_s_;
  std::string last_mapping_used_;
public:
  FourWay(ISerial& s, Log& l, double timeout_s=1.0, bool trace=false, double inter_delay_s=0.0)
      : ser_(s), log_(l), timeout_s_(timeout_s), trace_(trace), inter_delay_s_(inter_delay_s) {}

  bool test_alive();
  std::optional<uint8_t> proto_ver();
  std::optional<std::pair<uint8_t,uint8_t>> iface_ver();
  std::string iface_name();
  bool set_mode_silabs();
  bool exit_interface();
  void reset_target(uint8_t target);
  bool reset_esc(int idx0, ResetMode mode = ResetMode::ExitBootloader);
  bool reset_selected(ResetMode mode = ResetMode::ExitBootloader);
  int last_selected_idx0() const { return last_selected_idx0_; }
  bool last_selection_ok() const { return last_selection_ok_; }
  void clear_reset_cache() { reset_cap_known_ = false; }

  bool select_target_session(int idx0,
                             MappingMode mode,
                             bool allow_alt_addresses,
                             int direct_attempts = 6,
                             int index_attempts = 3,
                             SelectionReport* out = nullptr);

  std::pair<std::optional<uint16_t>, bool> select_target(int target_index,
                                                         MappingMode mode,
                                                         bool allow_alt_addresses = true);

  MappingMode probe_mapping(int esc_count);
  std::vector<uint8_t> read(uint16_t addr, int n, int retries=6);

  bool write(uint16_t addr, const std::vector<uint8_t>& data);
  bool erase_page(uint16_t page_index);
  bool erase_all();

  std::vector<uint8_t> read_ee(uint16_t addr, int n);
  bool write_ee(uint16_t addr, const std::vector<uint8_t>& data);

  const std::string& last_mapping_used() const { return last_mapping_used_; }

  uint16_t last_direct_addr_used() const { return last_direct_addr_used_; }
  bool last_select_used_scan() const { return last_select_used_scan_; }

  void set_timeout(double s) { timeout_s_ = s; }
  double get_timeout() const { return timeout_s_; }

  void set_inter_delay(double s) { inter_delay_s_ = s; }
  double get_inter_delay() const { return inter_delay_s_; }

  void set_probe_sleep(double s) { probe_sleep_s_ = s; }
  double get_probe_sleep() const { return probe_sleep_s_; }

  std::optional<uint16_t> diagnostic_init_flash_direct(uint16_t addr,
                                                       int attempts = 3,
                                                       SelectionReport* out = nullptr);
  std::optional<uint16_t> diagnostic_init_flash_index_val(uint8_t val,
                                                          int attempts = 3,
                                                          SelectionReport* out = nullptr);

private:
  void tx(uint8_t cmd, uint16_t addr, const std::vector<uint8_t>& params);
  struct Rx { uint8_t cmd; uint16_t addr; std::vector<uint8_t> params; uint8_t ack; };
  Rx rx();
  std::pair<uint16_t,std::vector<uint8_t>> cmd(uint8_t cmd, uint16_t addr, const std::vector<uint8_t>& params, uint8_t* ack_out);

  int last_selected_idx0_{0};
  bool last_selection_ok_{false};
  uint16_t last_direct_addr_used_{0};
  bool last_select_used_scan_{false};

  bool reset_cap_known_{false};
  enum class ResetCap { TWO_BYTE, ONE_BYTE, ZERO_BYTE };
  ResetCap reset_cap_{ResetCap::ONE_BYTE};

  std::optional<MappingMode> auto_mapping_locked_;

  double probe_sleep_s_{0.15};

  std::string iface_name_cache_;
};
}
