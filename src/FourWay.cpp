#include "esctool/FourWay.h"
#include "esctool/Bluejay.h"
#include "esctool/Utils.h"
#include <thread>
#include <stdexcept>

using namespace esctool;

void FourWay::tx(uint8_t cmd, uint16_t addr, const std::vector<uint8_t>& params_in){
  std::vector<uint8_t> params = params_in.empty()? std::vector<uint8_t>{0x00} : params_in;
  uint8_t ln = (uint8_t)(params.size() & 0xFF);
  std::vector<uint8_t> body{0x2F, cmd, (uint8_t)(addr>>8), (uint8_t)(addr&0xFF), (uint8_t)(ln==0?0:ln)};
  body.insert(body.end(), params.begin(), params.end());
  uint16_t crc = crc16_xmodem(body.data(), body.size());
  body.push_back((uint8_t)(crc>>8)); body.push_back((uint8_t)(crc&0xFF));
  ser_.write(body.data(), body.size()); ser_.flush();
}

FourWay::Rx FourWay::rx(){
  auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s_);
  auto rd = [&](size_t n){ std::vector<uint8_t> buf(n); size_t got=0; while(got<n && std::chrono::steady_clock::now()<deadline){ got += ser_.read(buf.data()+got, n-got);} buf.resize(got); return buf; };
  for(;;){
    // seek '.' start byte
    for(;;){
      auto b=rd(1);
      if(b.empty()) throw std::runtime_error("4-way timeout (no valid frame)");
      if(b[0]==0x2E) break;
    }

    auto hdr = rd(4); if(hdr.size()!=4) throw std::runtime_error("4-way timeout (hdr)");
    uint8_t cmd=hdr[0], ah=hdr[1], al=hdr[2], ln=hdr[3];
    int plen = (ln==0)?256:ln;
    auto params = rd(plen); if((int)params.size()!=plen) throw std::runtime_error("4-way timeout (params)");
    auto ack = rd(1); auto crc = rd(2); if(ack.size()!=1 || crc.size()!=2) throw std::runtime_error("4-way timeout (tail)");

    uint16_t rx_crc = ((uint16_t)crc[0]<<8)|crc[1];

    std::vector<uint8_t> msg; msg.reserve(1+4+plen);
    msg.push_back(0x2E); msg.insert(msg.end(), hdr.begin(), hdr.end()); msg.insert(msg.end(), params.begin(), params.end());
    uint16_t calc = crc16_xmodem(msg.data(), msg.size());

    bool ok = (calc==rx_crc);
    if (!ok) {
      // some FCs include ACK in the CRC
      msg.push_back(ack[0]);
      uint16_t calc2 = crc16_xmodem(msg.data(), msg.size());
      ok = (calc2==rx_crc);
    }

    if (!ok) {
      if (trace_) log_.trace("4-way RX CRC mismatch (discarding frame)");
      continue;
    }

    if (inter_delay_s_>0) std::this_thread::sleep_for(std::chrono::duration<double>(inter_delay_s_));
    return Rx{cmd, (uint16_t)((ah<<8)|al), params, ack[0]};
  }
}

std::pair<uint16_t,std::vector<uint8_t>> FourWay::cmd(uint8_t c, uint16_t a, const std::vector<uint8_t>& p, uint8_t* ack_out){
  tx(c,a,p); auto r = rx(); if(r.cmd!=c) throw std::runtime_error("4-way echo mismatch"); if(ack_out) *ack_out = r.ack; return {r.addr, r.params};
}

bool FourWay::test_alive(){ uint8_t ack=0xFF; try{ cmd(CMD_InterfaceTestAlive,0,{0x00},&ack); return ack==ACK_OK; } catch(...) { return false; } }
std::optional<uint8_t> FourWay::proto_ver(){ uint8_t ack=0xFF; try{ auto r=cmd(CMD_ProtocolGetVersion,0,{0x00},&ack); return (ack==ACK_OK && !r.second.empty())? std::optional<uint8_t>(r.second[0]):std::nullopt; } catch(...) { return std::nullopt; } }
std::optional<std::pair<uint8_t,uint8_t>> FourWay::iface_ver(){ uint8_t ack=0xFF; try{ auto r=cmd(CMD_InterfaceGetVersion,0,{0x00},&ack); return (ack==ACK_OK && r.second.size()>=2)? std::make_optional(std::make_pair(r.second[0],r.second[1])):std::nullopt; } catch(...) { return std::nullopt; } }
std::string FourWay::iface_name(){
  if (!iface_name_cache_.empty()) return iface_name_cache_;
  uint8_t ack=0xFF;
  try{
    auto r=cmd(CMD_InterfaceGetName,0,{0x00},&ack);
    iface_name_cache_ = (ack==ACK_OK)? std::string((char*)r.second.data(), r.second.size()) : std::string();
    return iface_name_cache_;
  } catch(...) {
    return {};
  }
}
bool FourWay::set_mode_silabs(){ uint8_t ack=0xFF; try{ cmd(CMD_InterfaceSetMode,0,{0x01},&ack); return ack==ACK_OK; } catch(...) { return false; } }
bool FourWay::exit_interface(){ uint8_t ack=0xFF; try{ cmd(CMD_InterfaceExit,0,{0x00},&ack); return ack==ACK_OK; } catch(...) { return false; } }
void FourWay::reset_target(uint8_t t){ try{ uint8_t ack=0xFF; cmd(CMD_DeviceReset,0,{t},&ack); } catch(...) {} }

bool FourWay::reset_esc(int idx0, ResetMode mode) {
  uint8_t ch = (uint8_t)(idx0 & 0xFF);
  uint8_t md = (uint8_t)mode;
  const char* mode_str = (mode == ResetMode::ExitBootloader) ? "exit" : "restart";

  if (reset_cap_known_) {
    auto try_cached = [&]() -> bool {
      try {
        uint8_t ack = 0xFF;
        switch (reset_cap_) {
          case ResetCap::TWO_BYTE:
            cmd(CMD_DeviceReset, 0, {ch, md}, &ack);
            break;
          case ResetCap::ONE_BYTE:
            cmd(CMD_DeviceReset, 0, {ch}, &ack);
            break;
          case ResetCap::ZERO_BYTE:
            cmd(CMD_DeviceReset, 0, {}, &ack);
            break;
        }
        if (trace_) log_.trace(std::string("reset_esc: strategy=cached(") +
          (reset_cap_==ResetCap::TWO_BYTE?"2byte":reset_cap_==ResetCap::ONE_BYTE?"1byte":"0byte") +
          ") mode=" + mode_str + " ch=" + std::to_string(idx0) + " ack=" + ack_name(ack));
        return ack == ACK_OK;
      } catch (...) { return false; }
    };
    bool ok = try_cached();
    if (ok) return true;
    reset_cap_known_ = false;
  }

  // try formats in mode-dependent order
  struct Strategy { ResetCap cap; std::vector<uint8_t> params; const char* label; };
  std::vector<Strategy> order;
  if (mode == ResetMode::ExitBootloader) {
    order = {
      {ResetCap::TWO_BYTE,  {ch, md}, "2byte"},
      {ResetCap::ONE_BYTE,  {ch},     "1byte"},
      {ResetCap::ZERO_BYTE, {},       "0byte"},
    };
  } else {
    order = {
      {ResetCap::ONE_BYTE,  {ch},     "1byte"},
      {ResetCap::TWO_BYTE,  {ch, md}, "2byte"},
      {ResetCap::ZERO_BYTE, {},       "0byte"},
    };
  }

  for (auto& s : order) {
    try {
      uint8_t ack = 0xFF;
      cmd(CMD_DeviceReset, 0, s.params, &ack);
      if (ack == ACK_OK) {
        reset_cap_known_ = true;
        reset_cap_ = s.cap;
        if (trace_) log_.trace(std::string("reset_esc: strategy=") + s.label +
          " mode=" + mode_str + " ch=" + std::to_string(idx0) + " ack=OK (cached)");
        return true;
      }
      if (ack == ACK_I_INVALID_PARAM || ack == ACK_I_INVALID_CMD) {
        if (trace_) log_.trace(std::string("reset_esc: strategy=") + s.label +
          " mode=" + mode_str + " ch=" + std::to_string(idx0) + " ack=" + ack_name(ack) + " (unsupported, next)");
        continue;
      }
      if (trace_) log_.trace(std::string("reset_esc: strategy=") + s.label +
        " mode=" + mode_str + " ch=" + std::to_string(idx0) + " ack=" + ack_name(ack));
      return false;
    } catch (...) {
      if (trace_) log_.trace(std::string("reset_esc: strategy=") + s.label +
        " mode=" + mode_str + " ch=" + std::to_string(idx0) + " exception");
    }
  }

  if (trace_) log_.trace("reset_esc: ch=" + std::to_string(idx0) + " mode=" + mode_str + " all strategies failed");
  return false;
}

bool FourWay::reset_selected(ResetMode mode) {
  return reset_esc(last_selected_idx0_, mode);
}

bool FourWay::select_target_session(int idx0,
                                    MappingMode mode,
                                    bool allow_alt_addresses,
                                    int direct_attempts,
                                    int index_attempts,
                                    SelectionReport* out){
  last_selection_ok_ = false;
  last_mapping_used_.clear();
  last_direct_addr_used_ = 0;
  last_select_used_scan_ = false;

  if (out) *out = SelectionReport{};

  if (mode == MappingMode::AUTO && auto_mapping_locked_.has_value()) {
    mode = *auto_mapping_locked_;
  }

  auto set_ok = [&](const std::optional<uint16_t>& sig){
    last_selected_idx0_ = idx0;
    last_selection_ok_ = true;
    if (out) { out->ok = true; out->sig = sig; }
    return true;
  };

  auto sleep_probe = [&](int attempt){
    double s = probe_sleep_s_;
    if (s < 0.0) s = 0.0;
    s *= (1.0 + 0.25 * (double)attempt);
    if (s > 1.0) s = 1.0;
    if (s > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(s));
  };

  auto try_device_init = [&](uint16_t addr, uint8_t val, uint8_t* ack_out)->std::optional<uint16_t>{
    try{
      uint8_t ack = 0xFF;
      auto r = cmd(CMD_DeviceInitFlash, addr, {val}, &ack);
      if (ack_out) *ack_out = ack;
      if (ack==ACK_OK && r.second.size()>=2) return std::optional<uint16_t>((uint16_t)(r.second[1]<<8 | r.second[0]));
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", addr);
        log_.trace(std::string("select: initFlash failed ack=0x") + [&](){ char a[4]; snprintf(a,4,"%02X", ack); return std::string(a);}() +
                   " (" + ack_name(ack) + ") addr=0x" + b + " len=" + std::to_string(r.second.size()));
      }
    } catch(const std::exception& e) {
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", addr);
        log_.trace(std::string("select: initFlash exception addr=0x") + b + " what=" + e.what());
      }
    } catch(...) {
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", addr);
        log_.trace(std::string("select: initFlash exception addr=0x") + b);
      }
    }
    return std::nullopt;
  };

  auto validate_selected = [&](uint16_t sig)->bool{
    uint16_t base = eeprom_base_for(sig);
    for (int t=0; t<4; ++t) {
      auto buf = read(base, 8, /*retries=*/2);
      if ((int)buf.size() == 8) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(15 + t * 10));
    }
    return false;
  };

  const std::string nm = iface_name();
  const bool prefer_direct = allow_alt_addresses && (nm.find("m4wFCIntf") != std::string::npos);

  auto try_direct = [&]()->bool{
    if (!prefer_direct) return false;
    uint16_t direct = (uint16_t)(0x0004u + (uint16_t)(idx0 & 0xFF));
    for (int a=0; a<std::max(1,direct_attempts); ++a) {
      if (out) out->attempts++;
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", direct);
        log_.trace(std::string("select: try direct addr=0x") + b + " attempt=" + std::to_string(a+1) + "/" + std::to_string(std::max(1,direct_attempts)));
      }
      uint8_t ack = 0xFF;
      if (auto sig = try_device_init(direct, 0x00, &ack)) {
        if (!validate_selected(*sig)) {
          if (trace_) log_.trace("select: direct init succeeded but validation read failed; retrying");
          sleep_probe(a);
          continue;
        }
        last_direct_addr_used_ = direct;
        if (out) { out->used_direct = true; out->direct_addr = direct; }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        return set_ok(sig);
      }
      sleep_probe(a);
    }
    return false;
  };

  auto index_val = [&](bool one_based)->uint8_t{ return one_based ? (uint8_t)((idx0+1)&0xFF) : (uint8_t)(idx0&0xFF); };

  auto try_index = [&](bool one_based)->bool{
    uint8_t val = index_val(one_based);
    const char* map_s = one_based ? "1-based" : "0-based";
    for (int a=0; a<std::max(1,index_attempts); ++a) {
      if (out) out->attempts++;
      if (trace_) {
        log_.trace(std::string("select: try index val=") + std::to_string((int)val) + " (" + map_s + ") attempt=" +
                   std::to_string(a+1) + "/" + std::to_string(std::max(1,index_attempts)));
      }
      uint8_t ack = 0xFF;
      if (auto sig = try_device_init(0, val, &ack)) {
        if (!validate_selected(*sig)) {
          if (trace_) log_.trace("select: index init succeeded but validation read failed; retrying");
          sleep_probe(a);
          continue;
        }
        last_mapping_used_ = map_s;
        if (out) { out->used_index = true; out->index_val = val; out->index_mapping = map_s; }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        return set_ok(sig);
      }

      if (ack == ACK_D_INVALID_CHANNEL) {
        return false;
      }
      sleep_probe(a);
    }
    return false;
  };

  if (mode == MappingMode::AUTO && !auto_mapping_locked_.has_value()) {
    if (idx0 != 0) {
      log_.info("WARNING: mapping=auto not yet resolved; refusing ambiguous selection for ESC" + std::to_string(idx0+1) + ". Use --mapping strict-0-based/strict-1-based or probe ESC1 first.");
      return false;
    }

    if (try_index(false)) { auto_mapping_locked_ = MappingMode::STRICT_0_BASED; return true; }
    if (try_index(true))  { auto_mapping_locked_ = MappingMode::STRICT_1_BASED; return true; }
    return false;
  }

  const bool prefer_one = (mode == MappingMode::AUTO || mode == MappingMode::PREFER_1_BASED || mode == MappingMode::STRICT_1_BASED);
  const bool strict = (mode == MappingMode::STRICT_0_BASED || mode == MappingMode::STRICT_1_BASED);

  if (!strict) {
    if (try_direct()) return true;
  }

  if (strict) {
    if (prefer_one) return try_index(true);
    return try_index(false);
  }

  if (prefer_one) { if (try_index(true)) return true; if (try_index(false)) return true; }
  else { if (try_index(false)) return true; if (try_index(true)) return true; }

  if (mode == MappingMode::AUTO && allow_alt_addresses) {
    log_.info("WARNING: selection degraded to scan; ESC identity not guaranteed");
    last_select_used_scan_ = true;
    for (uint16_t alt : {0x0004,0x0005,0x0006,0x0007,0x0008}) {
      if (out) out->attempts++;
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", alt);
        log_.trace(std::string("select: try scan addr=0x") + b);
      }
      uint8_t ack = 0xFF;
      if (auto sig = try_device_init(alt, 0x00, &ack)) {
        last_direct_addr_used_ = alt;
        if (out) { out->used_direct = true; out->direct_addr = alt; }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        return set_ok(sig);
      }
      sleep_probe(0);
    }
  }

  return false;
}

std::pair<std::optional<uint16_t>, bool> FourWay::select_target(int idx, MappingMode mode, bool allow_alt_addresses){
  SelectionReport rep;
  bool ok = select_target_session(idx, mode, allow_alt_addresses, /*direct_attempts=*/6, /*index_attempts=*/3, &rep);
  return {rep.sig, ok};
}

std::optional<uint16_t> FourWay::diagnostic_init_flash_direct(uint16_t addr,
                                                             int attempts,
                                                             SelectionReport* out){
  last_mapping_used_.clear();
  last_direct_addr_used_ = 0;
  last_select_used_scan_ = false;
  if (out) *out = SelectionReport{};

  auto sleep_probe = [&](int attempt){
    double s = probe_sleep_s_;
    if (s < 0.0) s = 0.0;
    s *= (1.0 + 0.25 * (double)attempt);
    if (s > 1.0) s = 1.0;
    if (s > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(s));
  };

  for (int a=0; a<std::max(1,attempts); ++a) {
    if (out) out->attempts++;
    try{
      uint8_t ack = 0xFF;
      auto r = cmd(CMD_DeviceInitFlash, addr, {0x00}, &ack);
      if (ack==ACK_OK && r.second.size()>=2) {
        uint16_t sig = (uint16_t)(r.second[1]<<8 | r.second[0]);
        last_direct_addr_used_ = addr;
        if (out) { out->ok = true; out->sig = sig; out->used_direct = true; out->direct_addr = addr; }
        return sig;
      }
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", addr);
        log_.trace(std::string("diag: initFlash direct failed ack=0x") + [&](){ char ab[4]; snprintf(ab,4,"%02X", ack); return std::string(ab);}() +
                   " (" + ack_name(ack) + ") addr=0x" + b + " len=" + std::to_string(r.second.size()));
      }
    } catch(const std::exception& e) {
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", addr);
        log_.trace(std::string("diag: initFlash direct exception addr=0x") + b + " what=" + e.what());
      }
    } catch(...) {
      if (trace_) {
        char b[8]; snprintf(b,8,"%04X", addr);
        log_.trace(std::string("diag: initFlash direct exception addr=0x") + b);
      }
    }
    sleep_probe(a);
  }
  return std::nullopt;
}

std::optional<uint16_t> FourWay::diagnostic_init_flash_index_val(uint8_t val,
                                                                 int attempts,
                                                                 SelectionReport* out){
  last_mapping_used_.clear();
  last_direct_addr_used_ = 0;
  last_select_used_scan_ = false;
  if (out) *out = SelectionReport{};

  auto sleep_probe = [&](int attempt){
    double s = probe_sleep_s_;
    if (s < 0.0) s = 0.0;
    s *= (1.0 + 0.25 * (double)attempt);
    if (s > 1.0) s = 1.0;
    if (s > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(s));
  };

  for (int a=0; a<std::max(1,attempts); ++a) {
    if (out) out->attempts++;
    try{
      uint8_t ack = 0xFF;
      auto r = cmd(CMD_DeviceInitFlash, 0, {val}, &ack);
      if (ack==ACK_OK && r.second.size()>=2) {
        uint16_t sig = (uint16_t)(r.second[1]<<8 | r.second[0]);
        if (out) { out->ok = true; out->sig = sig; out->used_index = true; out->index_val = val; }
        return sig;
      }
      if (trace_) {
        log_.trace(std::string("diag: initFlash index failed ack=0x") + [&](){ char ab[4]; snprintf(ab,4,"%02X", ack); return std::string(ab);}() +
                   " (" + ack_name(ack) + ") val=" + std::to_string((int)val) + " len=" + std::to_string(r.second.size()));
      }

      if (ack == ACK_D_INVALID_CHANNEL) {
        return std::nullopt;
      }
    } catch(const std::exception& e) {
      if (trace_) {
        log_.trace(std::string("diag: initFlash index exception val=") + std::to_string((int)val) + " what=" + e.what());
      }
    } catch(...) {
      if (trace_) {
        log_.trace(std::string("diag: initFlash index exception val=") + std::to_string((int)val));
      }
    }
    sleep_probe(a);
  }
  return std::nullopt;
}

MappingMode FourWay::probe_mapping(int){
  return MappingMode::AUTO;
}

std::vector<uint8_t> FourWay::read(uint16_t addr, int n, int retries){
  uint8_t ln = (uint8_t)(n & 0xFF); if (ln==0) ln=0; // 0 means 256
  for (int i=0;i<retries;++i){
    try{
      uint8_t ack=0xFF;
      auto r = cmd(CMD_DeviceRead, addr, {ln}, &ack);
      int want_n = (ln==0)?256:ln;
      if (ack==ACK_OK && r.first==addr && (int)r.second.size()==want_n) return r.second;

      if (trace_) {
        char a0[8]; snprintf(a0,8,"%04X", addr);
        char a1[8]; snprintf(a1,8,"%04X", (uint16_t)r.first);
        char ab[8]; snprintf(ab,8,"%02X", ack);
        log_.trace(std::string("4-way read fail try=") + std::to_string(i+1) +
                   " addr=0x" + a0 +
                   " n=" + std::to_string(want_n) +
                   " ack=0x" + ab + " (" + ack_name(ack) + ")" +
                   " r.addr=0x" + a1 +
                   " r.len=" + std::to_string(r.second.size()));
      }
    } catch(const std::exception& e){
      if (trace_) {
        char a0[8]; snprintf(a0,8,"%04X", addr);
        int want_n = (ln==0)?256:ln;
        log_.trace(std::string("4-way read exception try=") + std::to_string(i+1) +
                   " addr=0x" + a0 +
                   " n=" + std::to_string(want_n) +
                   " err='" + e.what() + "'");
      }
    } catch(...){
      if (trace_) {
        char a0[8]; snprintf(a0,8,"%04X", addr);
        int want_n = (ln==0)?256:ln;
        log_.trace(std::string("4-way read exception try=") + std::to_string(i+1) +
                   " addr=0x" + a0 +
                   " n=" + std::to_string(want_n) +
                   " err='<unknown>'");
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return {};
}

bool FourWay::write(uint16_t addr, const std::vector<uint8_t>& data){
  std::vector<uint8_t> d = data.empty()? std::vector<uint8_t>{0x00} : data;
  try{ uint8_t ack=0xFF; cmd(CMD_DeviceWrite, addr, d, &ack); return ack==ACK_OK; } catch(...) { return false; }
}

bool FourWay::erase_page(uint16_t page_index){
  try{ uint8_t ack=0xFF; cmd(CMD_DevicePageErase, 0, { (uint8_t)(page_index & 0xFF) }, &ack); return ack==ACK_OK; } catch(...) { return false; }
}

bool FourWay::erase_all(){
  try{ uint8_t ack=0xFF; cmd(CMD_DeviceEraseAll, 0, {0x00}, &ack); return ack==ACK_OK; } catch(...) { return false; }
}

std::vector<uint8_t> FourWay::read_ee(uint16_t addr, int n){
  uint8_t ln = (uint8_t)(n & 0xFF); if (ln==0) ln=0;
  try{
    uint8_t ack=0xFF; auto r = cmd(CMD_DeviceReadEE, addr, {ln}, &ack);
    if (ack==ACK_OK && r.first==addr) return r.second;
  } catch(...) {}
  return {};
}

bool FourWay::write_ee(uint16_t addr, const std::vector<uint8_t>& data){
  std::vector<uint8_t> d = data.empty()? std::vector<uint8_t>{0x00} : data;
  try{
    uint8_t ack=0xFF; cmd(CMD_DeviceWriteEE, addr, d, &ack);
    return ack==ACK_OK;
  } catch(...) { return false; }
}
