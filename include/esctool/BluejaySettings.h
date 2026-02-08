#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <optional>
#include <vector>

namespace esctool {

struct BluejayRawEeprom {
  std::array<uint8_t, 0xFF> bytes{};
};

struct BluejayIdentity {
  uint8_t layout_version = 0;   // LAYOUT_REVISION @ 0x02
  uint8_t fw_main = 0;          // MAIN_REVISION @ 0x00
  uint8_t fw_sub = 0;           // SUB_REVISION @ 0x01
  std::string layout_name;      // LAYOUT @ 0x40 (16 bytes)
  std::string mcu_name;         // MCU @ 0x50 (16 bytes)
  std::string esc_name;         // NAME @ 0x60 (16 bytes)
  int pwm_khz = 0;              // PWM_FREQUENCY @ 0x0A (24/48/96/0=dynamic)
};

struct BluejaySettings {
  uint8_t startup_power_min = 0;    // 0x04
  uint8_t startup_power_max = 0;    // 0x07
  uint8_t commutation_timing = 0;   // 0x15
  uint8_t demag_compensation = 0;   // 0x1F
  uint8_t rpm_power_slope = 0;      // 0x09
  uint8_t beep_strength = 0;        // 0x1B
  uint8_t beacon_strength = 0;      // 0x1C
  uint8_t beacon_delay = 0;         // 0x1D
  uint8_t power_rating = 0;         // 0x29
  uint8_t temperature_protection = 0; // 0x23
  uint8_t force_edt_arm = 0;        // 0x2A
  uint8_t brake_on_stop = 0;        // 0x27
  uint8_t braking_strength = 0;     // 0x10
  uint8_t motor_direction = 0;      // 0x0B
  uint8_t led_control = 0;          // 0x28
  uint8_t startup_beep = 0;         // 0x05
  uint8_t dithering = 0;            // 0x06
  uint8_t pwm_frequency = 0;        // 0x0A
  uint8_t threshold_96to48 = 0;     // 0x2C
  uint8_t threshold_48to24 = 0;     // 0x2B
};

struct BluejaySettingsDisplay {
  int startup_power_min = 0;        // display value (1000..1125)
  int startup_power_max = 0;        // display value (1004..1300)
  std::string commutation_timing;   // e.g. "22.5° (MediumHigh)"
  std::string demag_compensation;   // e.g. "Low"
  std::string rpm_power_slope;      // e.g. "10x"
  int beep_strength = 0;            // 0..255
  int beacon_strength = 0;          // 0..255
  std::string beacon_delay;         // e.g. "2 minutes"
  std::string power_rating;         // e.g. "2S+"
  std::string temperature_protection; // e.g. "80 C"
  std::string force_edt_arm;        // "On" / "Off"
  std::string brake_on_stop;        // "On" / "Off"
  int braking_strength = 0;         // 0..255
  std::string motor_direction;      // e.g. "Normal"
  std::string led_control;          // e.g. "Off"
  std::string startup_beep;         // e.g. "Normal"
  std::string dithering;            // "On" / "Off"
  std::string pwm_frequency;        // e.g. "48kHz" or "Dynamic"
  int threshold_96to48 = 0;
  int threshold_48to24 = 0;
};

namespace BJ {
  constexpr uint8_t OFF_MAIN_REVISION       = 0x00;
  constexpr uint8_t OFF_SUB_REVISION        = 0x01;
  constexpr uint8_t OFF_LAYOUT_REVISION     = 0x02;
  constexpr uint8_t OFF_STARTUP_POWER_MIN   = 0x04;
  constexpr uint8_t OFF_STARTUP_BEEP        = 0x05;
  constexpr uint8_t OFF_DITHERING           = 0x06;
  constexpr uint8_t OFF_STARTUP_POWER_MAX   = 0x07;
  constexpr uint8_t OFF_RPM_POWER_SLOPE     = 0x09;
  constexpr uint8_t OFF_PWM_FREQUENCY       = 0x0A;
  constexpr uint8_t OFF_MOTOR_DIRECTION     = 0x0B;
  constexpr uint8_t OFF_BRAKING_STRENGTH    = 0x10;
  constexpr uint8_t OFF_COMMUTATION_TIMING  = 0x15;
  constexpr uint8_t OFF_BEEP_STRENGTH       = 0x1B;
  constexpr uint8_t OFF_BEACON_STRENGTH     = 0x1C;
  constexpr uint8_t OFF_BEACON_DELAY        = 0x1D;
  constexpr uint8_t OFF_DEMAG_COMPENSATION  = 0x1F;
  constexpr uint8_t OFF_TEMPERATURE_PROTECTION = 0x23;
  constexpr uint8_t OFF_BRAKE_ON_STOP       = 0x27;
  constexpr uint8_t OFF_LED_CONTROL         = 0x28;
  constexpr uint8_t OFF_POWER_RATING        = 0x29;
  constexpr uint8_t OFF_FORCE_EDT_ARM       = 0x2A;
  constexpr uint8_t OFF_THRESHOLD_48to24    = 0x2B;
  constexpr uint8_t OFF_THRESHOLD_96to48    = 0x2C;
  constexpr uint8_t OFF_LAYOUT              = 0x40;
  constexpr uint8_t OFF_MCU                 = 0x50;
  constexpr uint8_t OFF_NAME                = 0x60;
  constexpr uint8_t OFF_STARTUP_MELODY      = 0x70;

  constexpr uint8_t SIZE_LAYOUT             = 16;
  constexpr uint8_t SIZE_MCU                = 16;
  constexpr uint8_t SIZE_NAME               = 16;
  constexpr uint8_t SIZE_STARTUP_MELODY     = 128;

  constexpr size_t EEPROM_SIZE              = 0xFF;
}

bool bj_parse_identity(const BluejayRawEeprom& raw, BluejayIdentity* out);
bool bj_parse_settings(const BluejayRawEeprom& raw, const BluejayIdentity& id, BluejaySettings* out);
bool bj_make_display(const BluejaySettings& s, const BluejayIdentity& id, BluejaySettingsDisplay* out);

int bj_startup_power_min_to_display(uint8_t raw);
int bj_startup_power_max_to_display(uint8_t raw);
uint8_t bj_startup_power_min_from_display(int display);
uint8_t bj_startup_power_max_from_display(int display);

std::string bj_timing_label(uint8_t raw);
std::string bj_demag_label(uint8_t raw);
std::string bj_rpm_slope_label(uint8_t raw, uint8_t layout_version);
std::string bj_beacon_delay_label(uint8_t raw);
std::string bj_power_rating_label(uint8_t raw);
std::string bj_temperature_label(uint8_t raw);
std::string bj_motor_direction_label(uint8_t raw);
std::string bj_led_control_label(uint8_t raw);
std::string bj_startup_beep_label(uint8_t raw, uint8_t layout_version);
std::string bj_pwm_frequency_label(uint8_t raw);

struct SettingsPatch {
  std::optional<int> startup_power_min;   // display value
  std::optional<int> startup_power_max;   // display value
  std::optional<uint8_t> commutation_timing;
  std::optional<uint8_t> demag_compensation;
  std::optional<uint8_t> rpm_power_slope;
  std::optional<uint8_t> beep_strength;
  std::optional<uint8_t> beacon_strength;
  std::optional<uint8_t> beacon_delay;
  std::optional<uint8_t> power_rating;
  std::optional<uint8_t> temperature_protection;
  std::optional<uint8_t> force_edt_arm;
  std::optional<uint8_t> brake_on_stop;
  std::optional<uint8_t> braking_strength;
  std::optional<uint8_t> motor_direction;
  std::optional<uint8_t> led_control;
  std::optional<uint8_t> startup_beep;
  std::optional<uint8_t> dithering;
  std::optional<uint8_t> pwm_frequency;
  std::optional<uint8_t> threshold_96to48;
  std::optional<uint8_t> threshold_48to24;
};

bool bj_apply_patch(std::vector<uint8_t>& page, const SettingsPatch& patch);

bool bj_parse_set_arg(const std::string& arg, SettingsPatch* patch, std::string* err);

} // namespace esctool
