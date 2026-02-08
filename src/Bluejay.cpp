#include "esctool/Bluejay.h"
#include <regex>
#include <cmath>
#include <cctype>

using namespace esctool;

uint16_t esctool::eeprom_base_for(uint16_t sig){
  switch(sig){
    case 0xE8B5: return 0x3000;
    case 0xE8B1: case 0xE8B2: case 0xE8B3: case 0xE8B4: default: return 0x1A00;
  }
}

int esctool::bj_min_display(int raw){ return int(std::lround(1000.0 + raw * (1000.0/2047.0))); }
int esctool::bj_max_display(int raw){ return int(std::lround(1000.0 + raw * (1000.0/250.0))); }

std::optional<BJParsed> esctool::parse_bluejay_block(const std::vector<uint8_t>& block){
  if (block.size() < (size_t)BJ_LAYOUT_SIZE) return std::nullopt;
  BJParsed r{};
  r.main    = block[OFF_MAIN_REV];
  r.sub     = block[OFF_SUB_REV];
  r.min_raw = block[OFF_MIN_PWR];
  r.max_raw = block[OFF_MAX_PWR];
  r.name.clear();
  r.name.reserve(NAME_LEN);
  for (int i=0;i<NAME_LEN;++i){
    uint8_t b = block[OFF_NAME + i];
    if (b>=32 && b<=126) r.name.push_back((char)b);
    else r.name.push_back(' ');
  }
  while(!r.name.empty() && (r.name.back()=='\0' || r.name.back()==' ')) r.name.pop_back();
  return r;
}

BJBlockStatus esctool::classify_bluejay_block(const std::vector<uint8_t>& block, std::string* reason){
  if (block.size() < (size_t)BJ_LAYOUT_SIZE) {
    if (reason) *reason = "too short";
    return BJBlockStatus::CORRUPT;
  }

  int ff_count = 0;
  int zero_count = 0;
  for (auto b : block) {
    if (b == 0xFF) ++ff_count;
    if (b == 0x00) ++zero_count;
  }

  const double n = (double)block.size();
  const double ff_ratio = ff_count / n;
  const double z_ratio = zero_count / n;

  if (ff_ratio >= 0.90) {
    if (reason) *reason = std::to_string(ff_count) + "/" + std::to_string((int)block.size()) + " bytes are 0xFF";
    return BJBlockStatus::ERASED;
  }
  if (z_ratio >= 0.90) {
    if (reason) *reason = std::to_string(zero_count) + "/" + std::to_string((int)block.size()) + " bytes are 0x00";
    return BJBlockStatus::ERASED;
  }

  if (block[OFF_MAIN_REV] == 0xFF && block[OFF_SUB_REV] == 0xFF) {
    if (reason) *reason = "version bytes are 0xFF";
    return BJBlockStatus::ERASED;
  }

  auto parsed = parse_bluejay_block(block);
  std::string name = parsed? parsed->name : std::string();
  auto tgt = extract_target(block);
  auto khz = extract_pwm_khz(block);

  bool version_ok = (block[OFF_MAIN_REV] != 0xFF && block[OFF_SUB_REV] != 0xFF);
  bool has_identity = (tgt.has_value() || !name.empty());
  bool khz_ok = khz.has_value();

  // If it has no identity markers and version bytes are suspicious, treat as corrupt.
  if (!has_identity && !khz_ok && !version_ok) {
    if (reason) *reason = "no tags/name, no PWM freq, and version bytes look invalid";
    return BJBlockStatus::CORRUPT;
  }

  if (reason) *reason = "looks valid";
  return BJBlockStatus::OK;
}

std::pair<std::string,std::string> esctool::split_name_and_patch(const std::string& name){
  static const std::regex rx("^\\s*([A-Za-z]+)\\s*(?:\\((.*?)\\))?\\s*$");
  std::smatch m; if (!std::regex_match(name, m, rx)) return {name, ""};
  std::string base = m[1]; std::string patch = m[2];
  return {base, patch};
}

std::string esctool::build_version(int main, int sub, const std::string& patch){
  std::string suffix;
  if (main > 0 || sub >= 20) {
    if (patch.empty()) suffix = ".0";
    else if (!patch.empty() && patch[0]=='.') suffix = patch;
    else suffix = std::string(".")+patch;
  }
  return "v" + std::to_string(main) + "." + std::to_string(sub) + suffix;
}

std::optional<std::string> esctool::extract_target(const std::vector<uint8_t>& block){
  // project bytes to safe ASCII
  std::string txt; txt.reserve(block.size());
  for (auto b : block) txt.push_back( (b>=32 && b<=126)? char(b) : ' ' );
  std::regex tag("#([A-Za-z0-9_]{3,32})#");
  auto begin= std::sregex_iterator(txt.begin(), txt.end(), tag);
  auto end  = std::sregex_iterator();
  for (auto it=begin; it!=end; ++it){
    std::string raw = (*it)[1].str();
    std::string upper = raw; for (auto& c:upper) c = (char)toupper(c);
    if (raw.rfind("BLHELI",0)==0 || upper.rfind("BLUEJAY",0)==0) continue;
    if (raw.find('_')==std::string::npos) continue;
    bool has_digit=false; for(char c:raw) if (isdigit((unsigned char)c)) { has_digit=true; break; }
    if (!has_digit) continue;
    std::string norm; norm.reserve(raw.size());
    std::string part;
    for (size_t i=0;i<raw.size();++i){
      if (raw[i]=='_'){
        // emit part
        if (!part.empty()){
          bool all_dig=true; for(char c:part) if(!isdigit((unsigned char)c)){ all_dig=false; break; }
          if (all_dig){ size_t j=0; while (j<part.size() && part[j]=='0') ++j; part = (j==part.size())? std::string("0") : part.substr(j); }
          if(!norm.empty()) norm.push_back('-');
          norm += part; part.clear();
        } else { if(!norm.empty()) norm.push_back('-'); }
      } else part.push_back(raw[i]);
    }
    if (!part.empty()){
      bool all_dig=true; for(char c:part) if(!isdigit((unsigned char)c)){ all_dig=false; break; }
      if (all_dig){ size_t j=0; while (j<part.size() && part[j]=='0') ++j; part = (j==part.size())? std::string("0") : part.substr(j); }
      if(!norm.empty()) norm.push_back('-');
      norm += part;
    }
    return norm;
  }
  return std::nullopt;
}

static std::optional<int> parse_khz_from_ascii_tags(const std::vector<uint8_t>& block){
  std::string txt; txt.reserve(block.size());
  for (auto b : block) txt.push_back( (b>=32 && b<=126)? char(b) : ' ' );
  // Look for either plain "48kHz" (any case) or a hashtag that contains 48K or 48KHZ
  std::regex rx1("([0-9]{2,3})\\s*[kK][hH]?[zZ]");
  std::smatch m;
  if (std::regex_search(txt, m, rx1)) {
    int v = std::stoi(m[1]);
    if (v>=8 && v<=384) return v;
  }
  std::regex tag("#([A-Za-z0-9_]{3,32})#");
  auto begin= std::sregex_iterator(txt.begin(), txt.end(), tag);
  auto end  = std::sregex_iterator();
  for (auto it=begin; it!=end; ++it){
    std::string raw = (*it)[1].str();
    std::string upper = raw; for (auto& c:upper) c = (char)toupper(c);
    std::smatch m2;
    if (std::regex_search(upper, m2, std::regex("([0-9]{2,3})K(HZ)?"))) {
      int v = std::stoi(m2[1]);
      if (v>=8 && v<=384) return v;
    }
  }
  return std::nullopt;
}

std::optional<int> esctool::extract_pwm_khz(const std::vector<uint8_t>& block){
  if (block.size() < (size_t)BJ_LAYOUT_SIZE) return std::nullopt;
  auto b = block[OFF_PWM_FREQ];
  int cand = -1;
  if (b==24 || b==48 || b==96 || b==192) cand = b;
  else if (b<=3) {
    static const int lut[4] = {24, 48, 96, 24};
    cand = lut[b];
  }
  if (cand>0) return cand;

  return parse_khz_from_ascii_tags(block);
}

// EEPROM byte -> label conversion

std::optional<std::string> esctool::extract_motor_timing_label(const std::vector<uint8_t>& block){
  if (block.size() <= OFF_COMM_TIMING) return std::nullopt;
  uint8_t v = block[OFF_COMM_TIMING];
  switch(v){
    case 1: return std::string("0 deg (Low)");
    case 2: return std::string("7.5 deg (MediumLow)");
    case 3: return std::string("15 deg (Medium)");
    case 4: return std::string("22.5 deg (MediumHigh)");
    case 5: return std::string("30 deg (High)");
    default: return std::nullopt;
  }
}

std::optional<std::string> esctool::extract_demag_label(const std::vector<uint8_t>& block){
  if (block.size() <= OFF_DEMAG) return std::nullopt;
  uint8_t v = block[OFF_DEMAG];
  switch(v){
    case 1: return std::string("Off");
    case 2: return std::string("Low");
    case 3: return std::string("High");
    default: return std::nullopt;
  }
}

std::optional<bool> esctool::extract_low_rpm_pp(const std::vector<uint8_t>& block){
  if (block.size() <= OFF_LRPM_PP) return std::nullopt;
  return block[OFF_LRPM_PP] != 0;
}
