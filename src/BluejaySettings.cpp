#include "esctool/BluejaySettings.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace esctool {

static std::string trim_string(const uint8_t* data, size_t len) {
  std::string s;
  for (size_t i = 0; i < len; ++i) {
    if (data[i] == 0 || data[i] == 0xFF) break;
    s.push_back((char)data[i]);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
  return s;
}

bool bj_parse_identity(const BluejayRawEeprom& raw, BluejayIdentity* out) {
  if (!out) return false;
  out->fw_main = raw.bytes[BJ::OFF_MAIN_REVISION];
  out->fw_sub = raw.bytes[BJ::OFF_SUB_REVISION];
  out->layout_version = raw.bytes[BJ::OFF_LAYOUT_REVISION];
  out->layout_name = trim_string(&raw.bytes[BJ::OFF_LAYOUT], BJ::SIZE_LAYOUT);
  out->mcu_name = trim_string(&raw.bytes[BJ::OFF_MCU], BJ::SIZE_MCU);
  out->esc_name = trim_string(&raw.bytes[BJ::OFF_NAME], BJ::SIZE_NAME);

  uint8_t pwm_raw = raw.bytes[BJ::OFF_PWM_FREQUENCY];
  if (pwm_raw == 24 || pwm_raw == 48 || pwm_raw == 96) {
    out->pwm_khz = (int)pwm_raw;
  } else if (pwm_raw == 0) {
    out->pwm_khz = 0; // dynamic
  } else {
    out->pwm_khz = (int)pwm_raw; // unknown, store raw
  }
  return true;
}

bool bj_parse_settings(const BluejayRawEeprom& raw, const BluejayIdentity& /*id*/, BluejaySettings* out) {
  if (!out) return false;
  out->startup_power_min = raw.bytes[BJ::OFF_STARTUP_POWER_MIN];
  out->startup_power_max = raw.bytes[BJ::OFF_STARTUP_POWER_MAX];
  out->commutation_timing = raw.bytes[BJ::OFF_COMMUTATION_TIMING];
  out->demag_compensation = raw.bytes[BJ::OFF_DEMAG_COMPENSATION];
  out->rpm_power_slope = raw.bytes[BJ::OFF_RPM_POWER_SLOPE];
  out->beep_strength = raw.bytes[BJ::OFF_BEEP_STRENGTH];
  out->beacon_strength = raw.bytes[BJ::OFF_BEACON_STRENGTH];
  out->beacon_delay = raw.bytes[BJ::OFF_BEACON_DELAY];
  out->power_rating = raw.bytes[BJ::OFF_POWER_RATING];
  out->temperature_protection = raw.bytes[BJ::OFF_TEMPERATURE_PROTECTION];
  out->force_edt_arm = raw.bytes[BJ::OFF_FORCE_EDT_ARM];
  out->brake_on_stop = raw.bytes[BJ::OFF_BRAKE_ON_STOP];
  out->braking_strength = raw.bytes[BJ::OFF_BRAKING_STRENGTH];
  out->motor_direction = raw.bytes[BJ::OFF_MOTOR_DIRECTION];
  out->led_control = raw.bytes[BJ::OFF_LED_CONTROL];
  out->startup_beep = raw.bytes[BJ::OFF_STARTUP_BEEP];
  out->dithering = raw.bytes[BJ::OFF_DITHERING];
  out->pwm_frequency = raw.bytes[BJ::OFF_PWM_FREQUENCY];
  out->threshold_96to48 = raw.bytes[BJ::OFF_THRESHOLD_96to48];
  out->threshold_48to24 = raw.bytes[BJ::OFF_THRESHOLD_48to24];
  return true;
}

int bj_startup_power_min_to_display(uint8_t raw) {
  double factor = 1000.0 / 2047.0;
  return (int)std::round((double)raw * factor + 1000.0);
}

uint8_t bj_startup_power_min_from_display(int display) {
  double factor = 1000.0 / 2047.0;
  double raw = ((double)display - 1000.0) / factor;
  int r = (int)std::round(raw);
  if (r < 0) r = 0;
  if (r > 255) r = 255;
  return (uint8_t)r;
}

int bj_startup_power_max_to_display(uint8_t raw) {
  double factor = 1000.0 / 250.0;
  return (int)std::round((double)raw * factor + 1000.0);
}

uint8_t bj_startup_power_max_from_display(int display) {
  double factor = 1000.0 / 250.0;
  double raw = ((double)display - 1000.0) / factor;
  int r = (int)std::round(raw);
  if (r < 0) r = 0;
  if (r > 255) r = 255;
  return (uint8_t)r;
}

std::string bj_timing_label(uint8_t raw) {
  switch (raw) {
    case 1: return "0 deg (Low)";
    case 2: return "7.5 deg (MediumLow)";
    case 3: return "15 deg (Medium)";
    case 4: return "22.5 deg (MediumHigh)";
    case 5: return "30 deg (High)";
    default: return "Unknown (" + std::to_string(raw) + ")";
  }
}

std::string bj_demag_label(uint8_t raw) {
  switch (raw) {
    case 1: return "Off";
    case 2: return "Low";
    case 3: return "High";
    default: return "Unknown (" + std::to_string(raw) + ")";
  }
}

std::string bj_rpm_slope_label(uint8_t raw, uint8_t layout_version) {
  if (layout_version >= 201) {
    // v0.10+ uses 1x..13x scale, 0=Off
    if (raw == 0) return "Off";
    if (raw >= 1 && raw <= 13) return std::to_string(raw) + "x";
    return "Unknown (" + std::to_string(raw) + ")";
  } else {
    // v0.9 (layout 200) uses different scale
    switch (raw) {
      case 1: return "0.5% (0.031)";
      case 7: return "5% (0.25)";
      case 8: return "7% (0.38)";
      case 9: return "10% (0.50)";
      case 10: return "15% (0.75)";
      case 11: return "20% (1.00)";
      case 12: return "24% (1.25)";
      case 13: return "29% (1.50)";
      default: return "Unknown (" + std::to_string(raw) + ")";
    }
  }
}

std::string bj_beacon_delay_label(uint8_t raw) {
  switch (raw) {
    case 1: return "1 minute";
    case 2: return "2 minutes";
    case 3: return "5 minutes";
    case 4: return "10 minutes";
    case 5: return "Infinite";
    default: return "Unknown (" + std::to_string(raw) + ")";
  }
}

std::string bj_power_rating_label(uint8_t raw) {
  switch (raw) {
    case 1: return "1S";
    case 2: return "2S+";
    default: return "Unknown (" + std::to_string(raw) + ")";
  }
}

std::string bj_temperature_label(uint8_t raw) {
  switch (raw) {
    case 0: return "Disabled";
    case 1: return "80 C";
    case 2: return "90 C";
    case 3: return "100 C";
    case 4: return "110 C";
    case 5: return "120 C";
    case 6: return "130 C";
    case 7: return "140 C";
    default: return "Unknown (" + std::to_string(raw) + ")";
  }
}

std::string bj_motor_direction_label(uint8_t raw) {
  switch (raw) {
    case 1: return "Normal";
    case 2: return "Reversed";
    case 3: return "Forward/Reverse (3D mode)";
    case 4: return "Forward/Reverse (3D mode) Reversed";
    default: return "Unknown (" + std::to_string(raw) + ")";
  }
}

std::string bj_led_control_label(uint8_t raw) {
  switch (raw) {
    case 0x00: return "Off";
    case 0x03: return "Blue";
    case 0x0C: return "Green";
    case 0x30: return "Red";
    case 0x0F: return "Cyan";
    case 0x33: return "Magenta";
    case 0x3C: return "Yellow";
    case 0x3F: return "White";
    default: return "Unknown (" + std::to_string(raw) + ")";
  }
}

std::string bj_startup_beep_label(uint8_t raw, uint8_t layout_version) {
  if (layout_version >= 205) {
    switch (raw) {
      case 0: return "Off";
      case 1: return "Normal";
      case 2: return "Custom";
      default: return "Unknown (" + std::to_string(raw) + ")";
    }
  } else {
    return raw ? "On" : "Off";
  }
}

std::string bj_pwm_frequency_label(uint8_t raw) {
  if (raw == 0) return "Dynamic";
  if (raw == 24 || raw == 48 || raw == 96) return std::to_string(raw) + "kHz";
  return std::to_string(raw) + "kHz";
}

bool bj_make_display(const BluejaySettings& s, const BluejayIdentity& id, BluejaySettingsDisplay* out) {
  if (!out) return false;

  out->startup_power_min = bj_startup_power_min_to_display(s.startup_power_min);
  out->startup_power_max = bj_startup_power_max_to_display(s.startup_power_max);
  out->commutation_timing = bj_timing_label(s.commutation_timing);
  out->demag_compensation = bj_demag_label(s.demag_compensation);
  out->rpm_power_slope = bj_rpm_slope_label(s.rpm_power_slope, id.layout_version);
  out->beep_strength = (int)s.beep_strength;
  out->beacon_strength = (int)s.beacon_strength;
  out->beacon_delay = bj_beacon_delay_label(s.beacon_delay);
  out->power_rating = bj_power_rating_label(s.power_rating);
  out->temperature_protection = bj_temperature_label(s.temperature_protection);
  out->force_edt_arm = s.force_edt_arm ? "On" : "Off";
  out->brake_on_stop = s.brake_on_stop ? "On" : "Off";
  out->braking_strength = (int)s.braking_strength;
  out->motor_direction = bj_motor_direction_label(s.motor_direction);
  out->led_control = bj_led_control_label(s.led_control);
  out->startup_beep = bj_startup_beep_label(s.startup_beep, id.layout_version);
  out->dithering = s.dithering ? "On" : "Off";
  out->pwm_frequency = bj_pwm_frequency_label(s.pwm_frequency);
  out->threshold_96to48 = (int)s.threshold_96to48;
  out->threshold_48to24 = (int)s.threshold_48to24;

  return true;
}

bool bj_apply_patch(std::vector<uint8_t>& page, const SettingsPatch& patch) {
  if (page.size() < BJ::EEPROM_SIZE) return false;

  if (patch.startup_power_min.has_value()) {
    page[BJ::OFF_STARTUP_POWER_MIN] = bj_startup_power_min_from_display(*patch.startup_power_min);
  }
  if (patch.startup_power_max.has_value()) {
    page[BJ::OFF_STARTUP_POWER_MAX] = bj_startup_power_max_from_display(*patch.startup_power_max);
  }
  if (patch.commutation_timing.has_value()) {
    page[BJ::OFF_COMMUTATION_TIMING] = *patch.commutation_timing;
  }
  if (patch.demag_compensation.has_value()) {
    page[BJ::OFF_DEMAG_COMPENSATION] = *patch.demag_compensation;
  }
  if (patch.rpm_power_slope.has_value()) {
    page[BJ::OFF_RPM_POWER_SLOPE] = *patch.rpm_power_slope;
  }
  if (patch.beep_strength.has_value()) {
    page[BJ::OFF_BEEP_STRENGTH] = *patch.beep_strength;
  }
  if (patch.beacon_strength.has_value()) {
    page[BJ::OFF_BEACON_STRENGTH] = *patch.beacon_strength;
  }
  if (patch.beacon_delay.has_value()) {
    page[BJ::OFF_BEACON_DELAY] = *patch.beacon_delay;
  }
  if (patch.power_rating.has_value()) {
    page[BJ::OFF_POWER_RATING] = *patch.power_rating;
  }
  if (patch.temperature_protection.has_value()) {
    page[BJ::OFF_TEMPERATURE_PROTECTION] = *patch.temperature_protection;
  }
  if (patch.force_edt_arm.has_value()) {
    page[BJ::OFF_FORCE_EDT_ARM] = *patch.force_edt_arm;
  }
  if (patch.brake_on_stop.has_value()) {
    page[BJ::OFF_BRAKE_ON_STOP] = *patch.brake_on_stop;
  }
  if (patch.braking_strength.has_value()) {
    page[BJ::OFF_BRAKING_STRENGTH] = *patch.braking_strength;
  }
  if (patch.motor_direction.has_value()) {
    page[BJ::OFF_MOTOR_DIRECTION] = *patch.motor_direction;
  }
  if (patch.led_control.has_value()) {
    page[BJ::OFF_LED_CONTROL] = *patch.led_control;
  }
  if (patch.startup_beep.has_value()) {
    page[BJ::OFF_STARTUP_BEEP] = *patch.startup_beep;
  }
  if (patch.dithering.has_value()) {
    page[BJ::OFF_DITHERING] = *patch.dithering;
  }
  if (patch.pwm_frequency.has_value()) {
    page[BJ::OFF_PWM_FREQUENCY] = *patch.pwm_frequency;
  }
  if (patch.threshold_96to48.has_value()) {
    page[BJ::OFF_THRESHOLD_96to48] = *patch.threshold_96to48;
  }
  if (patch.threshold_48to24.has_value()) {
    page[BJ::OFF_THRESHOLD_48to24] = *patch.threshold_48to24;
  }

  return true;
}

static std::string to_upper(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = (char)std::toupper((unsigned char)c);
  return r;
}

static bool parse_bool(const std::string& val, uint8_t* out) {
  std::string v = to_upper(val);
  if (v == "ON" || v == "1" || v == "TRUE" || v == "YES") { *out = 1; return true; }
  if (v == "OFF" || v == "0" || v == "FALSE" || v == "NO") { *out = 0; return true; }
  return false;
}

static bool parse_int(const std::string& val, int* out) {
  try {
    size_t pos = 0;
    int v = std::stoi(val, &pos);
    if (pos != val.size()) return false;
    *out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool bj_parse_set_arg(const std::string& arg, SettingsPatch* patch, std::string* err) {
  auto eq = arg.find('=');
  if (eq == std::string::npos) {
    if (err) *err = "Invalid format, expected KEY=VALUE";
    return false;
  }
  std::string key = to_upper(arg.substr(0, eq));
  std::string val = arg.substr(eq + 1);

  int ival = 0;
  uint8_t bval = 0;

  if (key == "STARTUP_POWER_MIN") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for STARTUP_POWER_MIN"; return false; }
    patch->startup_power_min = ival;
  } else if (key == "STARTUP_POWER_MAX") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for STARTUP_POWER_MAX"; return false; }
    patch->startup_power_max = ival;
  } else if (key == "COMMUTATION_TIMING" || key == "MOTOR_TIMING") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for COMMUTATION_TIMING"; return false; }
    patch->commutation_timing = (uint8_t)ival;
  } else if (key == "DEMAG_COMPENSATION" || key == "DEMAG") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for DEMAG_COMPENSATION"; return false; }
    patch->demag_compensation = (uint8_t)ival;
  } else if (key == "RPM_POWER_SLOPE" || key == "RAMPUP") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for RPM_POWER_SLOPE"; return false; }
    patch->rpm_power_slope = (uint8_t)ival;
  } else if (key == "BEEP_STRENGTH") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for BEEP_STRENGTH"; return false; }
    patch->beep_strength = (uint8_t)ival;
  } else if (key == "BEACON_STRENGTH") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for BEACON_STRENGTH"; return false; }
    patch->beacon_strength = (uint8_t)ival;
  } else if (key == "BEACON_DELAY") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for BEACON_DELAY"; return false; }
    patch->beacon_delay = (uint8_t)ival;
  } else if (key == "POWER_RATING") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for POWER_RATING"; return false; }
    patch->power_rating = (uint8_t)ival;
  } else if (key == "TEMPERATURE_PROTECTION" || key == "TEMP_PROTECTION") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for TEMPERATURE_PROTECTION"; return false; }
    patch->temperature_protection = (uint8_t)ival;
  } else if (key == "FORCE_EDT_ARM" || key == "EDT_ARM") {
    if (!parse_bool(val, &bval)) { if (err) *err = "Invalid bool for FORCE_EDT_ARM"; return false; }
    patch->force_edt_arm = bval;
  } else if (key == "BRAKE_ON_STOP") {
    if (!parse_bool(val, &bval)) { if (err) *err = "Invalid bool for BRAKE_ON_STOP"; return false; }
    patch->brake_on_stop = bval;
  } else if (key == "BRAKING_STRENGTH") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for BRAKING_STRENGTH"; return false; }
    patch->braking_strength = (uint8_t)ival;
  } else if (key == "MOTOR_DIRECTION" || key == "DIRECTION") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for MOTOR_DIRECTION"; return false; }
    patch->motor_direction = (uint8_t)ival;
  } else if (key == "LED_CONTROL" || key == "LED") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for LED_CONTROL"; return false; }
    patch->led_control = (uint8_t)ival;
  } else if (key == "STARTUP_BEEP") {
    if (parse_bool(val, &bval)) {
      patch->startup_beep = bval;
    } else if (parse_int(val, &ival)) {
      patch->startup_beep = (uint8_t)ival;
    } else {
      if (err) *err = "Invalid value for STARTUP_BEEP";
      return false;
    }
  } else if (key == "DITHERING") {
    if (!parse_bool(val, &bval)) { if (err) *err = "Invalid bool for DITHERING"; return false; }
    patch->dithering = bval;
  } else if (key == "PWM_FREQUENCY" || key == "PWM") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for PWM_FREQUENCY"; return false; }
    patch->pwm_frequency = (uint8_t)ival;
  } else if (key == "THRESHOLD_96TO48") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for THRESHOLD_96TO48"; return false; }
    patch->threshold_96to48 = (uint8_t)ival;
  } else if (key == "THRESHOLD_48TO24") {
    if (!parse_int(val, &ival)) { if (err) *err = "Invalid integer for THRESHOLD_48TO24"; return false; }
    patch->threshold_48to24 = (uint8_t)ival;
  } else {
    if (err) *err = "Unknown setting key: " + key;
    return false;
  }

  return true;
}

} // namespace esctool
