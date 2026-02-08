#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace esctool {
static constexpr int BJ_LAYOUT_SIZE = 0x100;
static constexpr int OFF_MAIN_REV   = 0x00;
static constexpr int OFF_SUB_REV    = 0x01;
static constexpr int OFF_MIN_PWR    = 0x04;
static constexpr int OFF_MAX_PWR    = 0x07;
static constexpr int OFF_NAME       = 0x60;
static constexpr int NAME_LEN       = 16;
static constexpr int OFF_PWM_FREQ   = 0x0A;

static constexpr int OFF_COMM_TIMING = 0x15;
static constexpr int OFF_DEMAG       = 0x1F;
static constexpr int OFF_LRPM_PP     = 0x24;

uint16_t eeprom_base_for(uint16_t sig);
int bj_min_display(int raw); // -> us
int bj_max_display(int raw);

struct BJParsed { std::string name; int main, sub, min_raw, max_raw; };
std::optional<BJParsed> parse_bluejay_block(const std::vector<uint8_t>& block);

enum class BJBlockStatus { OK, ERASED, CORRUPT };
BJBlockStatus classify_bluejay_block(const std::vector<uint8_t>& block, std::string* reason);

std::pair<std::string,std::string> split_name_and_patch(const std::string& name); // base, patch
std::string build_version(int main, int sub, const std::string& patch);
std::optional<std::string> extract_target(const std::vector<uint8_t>& block);

std::optional<int> extract_pwm_khz(const std::vector<uint8_t>& block);
std::optional<std::string> extract_motor_timing_label(const std::vector<uint8_t>& block);
std::optional<std::string> extract_demag_label(const std::vector<uint8_t>& block);
std::optional<bool>        extract_low_rpm_pp(const std::vector<uint8_t>& block);
}
