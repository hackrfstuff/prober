#pragma once
#include "esctool/Log.h"
#include "esctool/MSP.h"
#include "esctool/FourWay.h"
#include "esctool/IntelHex.h"
#include "esctool/Silabs.h"
#include "esctool/Bluejay.h"
#include "esctool/BluejaySettings.h"
#include <optional>
#include <string>

namespace esctool {

enum class VerifyMode { NONE=0, FULL=1, FAST=2 };
enum class SettingsMode { PRESERVE=0, ERASE=1, MIGRATE=2 };

struct MspRestoreResult {
  bool ok{false};
  int attempts{0};
  int elapsed_ms{0};
  std::string last_error;
};

struct ReadRow {
  int esc{0};
  std::optional<uint16_t> sig;
  int select_attempts{0};
  std::string mapping_used;
  BJBlockStatus status{BJBlockStatus::CORRUPT};
  std::string reason;
  std::optional<BJParsed> parsed;
  std::optional<std::string> target;
  std::optional<int> pwm_khz;
  std::optional<std::string> timing;
  std::optional<std::string> demag;
  std::optional<bool> lrpm;
};

struct ReadRowFull {
  int esc{0};
  std::optional<uint16_t> sig;
  int select_attempts{0};
  std::string mapping_used;
  bool ok{false};
  std::string error;
  BluejayIdentity identity;
  BluejaySettings settings;
  BluejaySettingsDisplay display;
  std::vector<uint8_t> raw_page;
};

class Flasher {
  ISerial& ser_; MSP& msp_; FourWay& fw_; Log& log_;
  bool select_index_only(int esc_index_0based, int tries, SelectionReport* out);
  bool select_settings_target(int esc_index_0based, int index_tries, int direct_tries, SelectionReport* out);
  bool read_block_with_recovery(int esc_index_0based,
                               uint16_t base,
                               size_t len,
                               size_t chunk_max,
                               std::vector<uint8_t>* out,
                               SelectionReport* last_rep,
                               bool allow_alt_addressing = true);
public:
  int    probe_tries{6};
  double probe_sleep{0.15};
  MappingMode mapping_mode{MappingMode::AUTO};
  SettingsMode settings_mode{SettingsMode::PRESERVE};

  bool delay_user_set{false};
  bool timeout_user_set{false};

  bool safe_mode{false};
  bool full_erase_app{false};
  bool full_erase_entire_app{false};
  bool dry_run{false};
  bool verify_all_bytes{false};

  int erase_retries{3};
  int erase_inter_page_ms{50};
  int write_retries{3};
  int write_inter_block_ms{10};
  int verify_read_retries{3};

  std::string last_error_;

  Flasher(ISerial& ser, MSP& msp, FourWay& fw, Log& log): ser_(ser), msp_(msp), fw_(fw), log_(log) {}

  std::optional<int> enter_passthrough();
  bool bringup_4way();

  MspRestoreResult exit_passthrough_and_restore_msp(int timeout_ms = 5000);

  bool flash_one(int esc_index_0based,
                 const std::string& hex_path,
                 VerifyMode verify_mode,
                 bool erase_eeprom,
                 std::optional<uint16_t> assume_sig = std::nullopt);

  bool read_one(int esc_index_0based, ReadRow* out);

  bool read_settings_full(int esc_index_0based, ReadRowFull* out, bool allow_alt_addressing = true);

  bool read_settings_page(int esc_index_0based, uint16_t base, size_t len, std::vector<uint8_t>* out);
  bool write_settings_page(int esc_index_0based, uint16_t base, const std::vector<uint8_t>& page);

  bool update_settings(int esc_index_0based, const SettingsPatch& patch, BluejaySettingsDisplay* after);
};
}
