#include "esctool/Flasher.h"
#include "esctool/Bluejay.h"
#include "esctool/BluejaySettings.h"
#include "esctool/Utils.h"
#include <thread>
#include <algorithm>
#include <functional>

using namespace esctool;

std::optional<int> Flasher::enter_passthrough(){
  {
    bool ok = false;
    for (int attempt=0; attempt<3 && !ok; ++attempt) {
      try{
        log_.info("MSP: API_VERSION...");
        auto api = msp_.req(MSP_API_VERSION);
        if(api.size()>=2) log_.info("MSP API version: "+std::to_string(api[0])+"."+std::to_string(api[1]));
        ok = true;
      } catch(const std::exception& e){
        log_.info(std::string("No MSP handshake: ")+e.what());
        if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }
    if (!ok) {
      log_.info("Tip: FC may still be exiting passthrough from a previous run; wait ~1s or unplug/replug USB.");
      return std::nullopt;
    }
  }
  try{ auto v = msp_.req(MSP_FC_VARIANT); std::string s((char*)v.data(), v.size()); log_.info("FC variant: "+s); }catch(...){ }
  try{ auto fv = msp_.req(MSP_FC_VERSION); if(fv.size()>=3) log_.info("FC version: "+std::to_string(fv[0])+"."+std::to_string(fv[1])+"."+std::to_string(fv[2])); }catch(...){ }
  for (int attempt=0; attempt<3; ++attempt) {
    try{
      log_.info("MSP: SET_PASSTHROUGH (245) -> 4-way...");
      auto pl = msp_.req(MSP_SET_PASSTHROUGH, {});
      int esc_count = pl.empty()? -1 : pl[0];
      log_.info(std::string("Passthrough OK. ESC count reported: ") + (esc_count>=0? std::to_string(esc_count): std::string("unknown")) );
      return esc_count>=0? std::optional<int>(esc_count): std::optional<int>();
    } catch(const std::exception& e){
      log_.info(std::string("Could not enter passthrough: ") + e.what());
      if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  log_.info("Tip: FC may still be exiting passthrough; wait ~1s or unplug/replug USB.");
  return std::nullopt;
}

MspRestoreResult Flasher::exit_passthrough_and_restore_msp(int timeout_ms) {
  MspRestoreResult result;
  auto t0 = std::chrono::steady_clock::now();
  auto elapsed = [&]() -> int {
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  };

  log_.info("Exiting 4-way interface...");
  for (int i = 0; i < 5; ++i) {
    fw_.reset_selected();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (fw_.exit_interface()) {
      log_.info("4-way exit OK (attempt " + std::to_string(i + 1) + ")");
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  log_.info("Draining serial input...");
  ser_.flush_input();
  {
    uint8_t drain_buf[256];
    auto drain_start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - drain_start).count() < 200) {
      size_t n = ser_.read(drain_buf, sizeof(drain_buf));
      if (n == 0) break;
    }
  }
  ser_.flush_input();

  log_.info("Polling MSP to verify FC restored...");
  while (elapsed() < timeout_ms) {
    result.attempts++;
    try {
      auto api = msp_.req(MSP_API_VERSION);
      if (api.size() >= 2) {
        result.ok = true;
        result.elapsed_ms = elapsed();
        log_.info("MSP restored after " + std::to_string(result.attempts) +
                  " attempts / " + std::to_string(result.elapsed_ms) + " ms");
        return result;
      }
    } catch (const std::exception& e) {
      result.last_error = e.what();
      if (log_.want(LogLevel::TRACE)) {
        log_.trace("MSP poll attempt " + std::to_string(result.attempts) +
                   " failed: " + e.what());
      }
    } catch (...) {
      result.last_error = "unknown exception";
    }
    int backoff = std::min(250, 100 + (result.attempts - 1) * 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
  }

  log_.info("MSP not responding; attempting serial port reopen...");
  ser_.flush_input();
  ser_.flush_output();
  if (ser_.reopen()) {
    log_.info("Serial port reopened; retrying MSP...");
    ser_.flush_input();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int reopen_deadline = timeout_ms + 3000; // extra 3s for reopen attempt
    while (elapsed() < reopen_deadline) {
      result.attempts++;
      try {
        auto api = msp_.req(MSP_API_VERSION);
        if (api.size() >= 2) {
          result.ok = true;
          result.elapsed_ms = elapsed();
          log_.info("MSP restored (after reopen) after " + std::to_string(result.attempts) +
                    " attempts / " + std::to_string(result.elapsed_ms) + " ms");
          return result;
        }
      } catch (const std::exception& e) {
        result.last_error = e.what();
      } catch (...) {
        result.last_error = "unknown exception";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  } else {
    log_.info("Serial port reopen failed");
    result.last_error = "serial reopen failed; " + result.last_error;
  }

  result.elapsed_ms = elapsed();
  log_.info("Failed to restore MSP after " + std::to_string(result.attempts) +
            " attempts / " + std::to_string(result.elapsed_ms) + " ms");
  log_.info("Last error: " + result.last_error);
  log_.info("FC likely still in passthrough or USB CDC wedged; power cycle may be required.");
  return result;
}

bool Flasher::bringup_4way(){
  if (!fw_.test_alive()){ log_.info("4-way: no response to TestAlive"); return false; }
  auto pv = fw_.proto_ver(); auto iv = fw_.iface_ver(); auto nm = fw_.iface_name();
  log_.info(std::string("4-way: Protocol v") + (pv? std::to_string(*pv):"?") + (iv? std::string(", Interface v")+std::to_string(iv->first)+"."+std::to_string(iv->second):"") + (nm.empty()?"":std::string(", Name='")+nm+"'"));
  fw_.set_mode_silabs();
  return true;
}

bool Flasher::select_index_only(int idx, int tries, SelectionReport* out){
  return fw_.select_target_session(idx,
                                  mapping_mode,
                                  /*allow_alt_addresses=*/false,
                                  /*direct_attempts=*/std::max(1, tries),
                                  /*index_attempts=*/std::max(1, tries),
                                  out);
}

bool Flasher::select_settings_target(int idx, int index_tries, int direct_tries, SelectionReport* out) {
  // Prefer index selection under the resolved strict mapping, but fall back to
  // direct addressing when the index path is flaky.
  SelectionReport rep;
  if (select_index_only(idx, std::max(1, index_tries), &rep)) {
    if (out) *out = rep;
    return true;
  }

  uint16_t direct = (uint16_t)(0x0004u + (uint16_t)(idx & 0xFF));
  log_.debug("select: ESC" + std::to_string(idx + 1) + " index select failed; falling back to direct addr=0x" +
             [&](){ char b[8]; snprintf(b, 8, "%04X", direct); return std::string(b); }());
  SelectionReport drep;
  auto sig = fw_.diagnostic_init_flash_direct(direct, std::max(1, direct_tries), &drep);
  if (!sig.has_value()) {
    if (out) *out = drep;
    return false;
  }

  // Validate the selection by reading from the Bluejay settings region.
  uint16_t base = eeprom_base_for(*sig);
  bool valid = false;
  for (int t = 0; t < 2; ++t) {
    auto buf = fw_.read(base, 16, /*retries=*/2);
    if ((int)buf.size() == 16) { valid = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!valid) {
    if (out) *out = drep;
    return false;
  }

  if (out) *out = drep;
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  return true;
}

bool Flasher::read_block_with_recovery(int idx,
                                      uint16_t base,
                                      size_t len,
                                      size_t chunk_max,
                                      std::vector<uint8_t>* out,
                                      SelectionReport* last_rep,
                                      bool allow_alt_addressing) {
  if (out) out->clear();

  auto backoff_ms = [&](int attempt)->int {
    int ms = 20;
    for (int i = 0; i < attempt; ++i) ms = std::min(250, ms * 2);
    return ms;
  };

  auto try_read_once = [&](size_t chunk_size, SelectionReport* rep)->bool {
    std::vector<uint8_t> data;
    data.reserve(len);

    bool sel_ok;
    if (allow_alt_addressing) {
      sel_ok = select_settings_target(idx, std::max(2, probe_tries), /*direct_tries=*/2, rep);
    } else {
      sel_ok = select_index_only(idx, std::max(2, probe_tries), rep);
    }
    if (!sel_ok) {
      return false;
    }

    uint16_t sig_base = eeprom_base_for(rep->sig.value_or(0));
    uint16_t actual_base = (sig_base != 0) ? sig_base : base;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (size_t off = 0; off < len; off += chunk_size) {
      size_t want = std::min(chunk_size, len - off);
      bool got_chunk = false;

      for (int ct = 0; ct < 3; ++ct) {
        auto chunk = fw_.read((uint16_t)(actual_base + off), (int)want, /*retries=*/2);
        if (chunk.size() == want) {
          data.insert(data.end(), chunk.begin(), chunk.end());
          got_chunk = true;
          break;
        }

        if (log_.want(LogLevel::TRACE)) {
          log_.trace("read_block: ESC" + std::to_string(idx + 1) + " off=" + std::to_string(off) +
                     " want=" + std::to_string(want) + " got=" + std::to_string(chunk.size()) +
                     " chunk_try=" + std::to_string(ct + 1) + "/3");
        }

        fw_.reset_esc(idx);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms(ct)));

        SelectionReport r2;
        bool resel_ok = allow_alt_addressing
          ? select_settings_target(idx, std::max(2, probe_tries), /*direct_tries=*/2, &r2)
          : select_index_only(idx, std::max(2, probe_tries), &r2);
        if (resel_ok) {
          *rep = r2;
          uint16_t sb = eeprom_base_for(rep->sig.value_or(0));
          if (sb != 0) actual_base = sb;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }

      if (!got_chunk) {
        return false;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (data.size() != len) return false;
    if (out) *out = std::move(data);
    return true;
  };

  std::vector<size_t> chunk_sizes;
  size_t cs = std::max<size_t>(16, chunk_max);
  while (cs > 16) {
    chunk_sizes.push_back(cs);
    cs /= 2;
    if (cs < 16) cs = 16;
  }
  chunk_sizes.push_back(16);

  for (size_t chunk_size : chunk_sizes) {
    for (int attempt = 0; attempt < 5; ++attempt) {
      SelectionReport rep;
      if (try_read_once(chunk_size, &rep)) {
        if (last_rep) *last_rep = rep;
        return true;
      }
      if (last_rep) *last_rep = rep;

      fw_.reset_esc(idx);
      std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms(attempt)));
    }
  }

  return false;
}

bool Flasher::read_one(int idx, ReadRow* out){
  if (out) *out = ReadRow{};
  if (out) out->esc = idx + 1;

  SelectionReport rep;
  bool ok = select_index_only(idx, std::max(1, probe_tries), &rep);
  if (out) {
    out->sig = rep.sig;
    out->select_attempts = rep.attempts;
    out->mapping_used = fw_.last_mapping_used();
  }
  if (!ok) {
    if (out) {
      out->status = BJBlockStatus::CORRUPT;
      out->reason = "select failed";
    }
    return false;
  }

  uint16_t base = eeprom_base_for(rep.sig.value_or(0));

  auto reselect = [&](){
    SelectionReport srep;
    (void)select_index_only(idx, std::max(1, probe_tries), &srep);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  };

  auto read_settings = [&]()->std::vector<uint8_t>{
    for (int attempt=0; attempt<3; ++attempt) {
      auto buf = fw_.read(base, BJ_LAYOUT_SIZE, /*retries=*/1);
      if ((int)buf.size() == BJ_LAYOUT_SIZE) return buf;
      reselect();
    }
    return {};
  };

  auto data = read_settings();
  if (data.empty()) {
    if (out) {
      out->status = BJBlockStatus::CORRUPT;
      out->reason = "read failed";
    }
    return true;
  }

  std::string reason;
  BJBlockStatus st = classify_bluejay_block(data, &reason);
  if (out) {
    out->status = st;
    out->reason = reason;
  }

  auto parsed = parse_bluejay_block(data);
  if (out) out->parsed = parsed;
  if (parsed) {
    if (out) {
      out->target = extract_target(data);
      out->pwm_khz = extract_pwm_khz(data);
      out->timing = extract_motor_timing_label(data);
      out->demag = extract_demag_label(data);
      out->lrpm = extract_low_rpm_pp(data);
    }
  }
  return true;
}

bool Flasher::flash_one(int idx, const std::string& hex_path, VerifyMode verify_mode, bool erase_eeprom, std::optional<uint16_t> assume_sig){
  last_error_.clear();
  log_.info("\n=== ESC"+std::to_string(idx+1)+": flashing '"+hex_path+"' ===");

  SelectionReport rep;
  bool ok = fw_.select_target_session(idx,
                                     mapping_mode,
                                     /*allow_alt_addresses=*/false,
                                     /*direct_attempts=*/std::max(1, probe_tries),
                                     /*index_attempts=*/std::max(1, probe_tries),
                                     &rep);
  std::optional<uint16_t> sig = rep.sig;
  if (!ok){
    if (assume_sig){ last_error_ = "bootloader select failed"; log_.info("Assuming MCU signature 0x"+ [&](){ char b[8]; snprintf(b,8,"%04X", *assume_sig); return std::string(b);}() + " (forced) — but bootloader select still failed, cannot flash via passthrough."); }
    else { last_error_ = "no response from ESC bootloader"; log_.info("No response from ESC bootloader; cannot flash via passthrough."); }
    return false;
  }

  if (fw_.last_select_used_scan()) {
    last_error_ = "selection degraded to scan";
    log_.info("Selection degraded to scan; aborting flash to avoid flashing the wrong ESC");
    return false;
  }

  if (fw_.last_direct_addr_used() != 0) {
    log_.info("select: direct addr=0x" + [&](){ char b[8]; snprintf(b,8,"%04X", fw_.last_direct_addr_used()); return std::string(b);}());
  } else if (!fw_.last_mapping_used().empty()) {
    int sel_val = (fw_.last_mapping_used()=="1-based") ? (idx+1) : idx;
    log_.info("select: index sel=" + std::to_string(sel_val) + " mapping=" + fw_.last_mapping_used());
  }

  uint16_t usig = sig.value_or(0);
  auto* m = silabs_from_sig(usig);
  if (!m){
    log_.info("Unknown/unsupported SiLabs signature 0x" + [&](){ char b[8]; snprintf(b,8,"%04X", usig); return std::string(b);}());
    if (auto iv = fw_.iface_ver()) log_.info("4-way interface v"+std::to_string(iv->first)+"."+std::to_string(iv->second));
    return false;
  }

  const bool is_bb51 = (usig == 0xE8B5);
  const bool safe = safe_mode || is_bb51;

  SettingsMode sm = settings_mode;
  if (erase_eeprom) sm = SettingsMode::ERASE;

  auto read_flash_range = [&](uint32_t addr, uint32_t len, int retries)->std::vector<uint8_t>{
    std::vector<uint8_t> out; out.reserve(len);
    for (uint32_t off=0; off<len; ){
      int chunk = (int)std::min<uint32_t>(256u, len - off);
      std::vector<uint8_t> got;
      for (int t=0; t<std::max(1,retries); ++t){
        got = fw_.read((uint16_t)(addr+off), chunk, /*retries=*/1);
        if ((int)got.size() == chunk) break;
        SelectionReport rrep;
        (void)fw_.select_target_session(idx, mapping_mode, /*allow_alt_addresses=*/false,
                                        /*direct_attempts=*/std::max(6, probe_tries),
                                        /*index_attempts=*/std::max(3, probe_tries),
                                        &rrep);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if ((int)got.size() != chunk) return {};
      out.insert(out.end(), got.begin(), got.end());
      off += (uint32_t)chunk;
    }
    return out;
  };

  bool applied_safe_defaults = false;
  if (safe) {
    if (!delay_user_set) { fw_.set_inter_delay(0.010); applied_safe_defaults = true; }
    if (!timeout_user_set) { fw_.set_timeout(6.0); applied_safe_defaults = true; }
  }
  if (applied_safe_defaults && is_bb51 && !safe_mode) {
    log_.info("BB51 safe defaults: delay=0.010s timeout=6s (override with --delay/--timeout)");
  }

  IntelHexImage ih; ih.load(hex_path);
  auto image = ih.build(m->firmware_start, m->bootloader_address);
  log_.info("MCU sig=0x" + [&](){ char b[8]; snprintf(b,8,"%04X", usig); return std::string(b);}() +
           " page="+std::to_string(m->page_size)+"B app=[0x"+ [&](){ char b[8]; snprintf(b,8,"%04X", m->firmware_start); return std::string(b);}() + "..0x" + [&](){ char b[8]; snprintf(b,8,"%04X", m->bootloader_address); return std::string(b);}() + ")");
  log_.info("Image bytes to program: "+std::to_string(image.size()));

  auto align_up = [](uint32_t v, uint32_t a)->uint32_t{ return (a==0)? v : ((v + a - 1) / a) * a; };
  const uint32_t flash_end_excl = align_up((uint32_t)m->lockbyte_address + 1u, (uint32_t)m->page_size);
  const uint32_t eeprom_start = (uint32_t)m->eeprom_offset;
  const uint32_t eeprom_end_excl = (uint32_t)m->eeprom_offset + (uint32_t)m->page_size;
  const uint32_t app_start = (uint32_t)m->firmware_start;
  const uint32_t app_end_excl = (uint32_t)m->bootloader_address;

  if (auto mx = ih.max_addr()) {
    if (*mx >= app_end_excl) {
      log_.info("Warning: HEX contains data outside app range (max addr 0x" + [&](){ char b[16]; snprintf(b,16,"%04X", (uint16_t)(*mx & 0xFFFF)); return std::string(b);}() + ") — bytes at/above 0x" + [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)app_end_excl); return std::string(b);}() + " are ignored");
    }
  }

  const uint32_t settings_start = std::max(eeprom_start, app_start);
  const uint32_t settings_end_excl = std::min(eeprom_end_excl, app_end_excl);

  auto settings_in_image = [&]()->bool{
    for (uint32_t a=settings_start; a<settings_end_excl; ++a) {
      if (image[a - m->firmware_start] != 0xFF) return true;
    }
    return false;
  };

  auto mask_settings_in_image = [&](){
    for (uint32_t a=settings_start; a<settings_end_excl; ++a) image[a - m->firmware_start] = 0xFF;
  };

  std::vector<uint8_t> old_settings_page;
  std::vector<uint8_t> new_settings_defaults;
  std::vector<uint8_t> settings_to_write;

  if (sm == SettingsMode::PRESERVE) {
    if (settings_in_image()) {
      log_.info("Warning: HEX includes data in settings page [0x" + [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)settings_start); return std::string(b);}() + "..0x" + [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)settings_end_excl); return std::string(b);}() + ") — preserving settings");
      mask_settings_in_image();
    }
  } else {
    mask_settings_in_image();
    new_settings_defaults = ih.build(eeprom_start, eeprom_end_excl);
    if (sm == SettingsMode::MIGRATE) {
      old_settings_page = read_flash_range(eeprom_start, (uint32_t)m->page_size, /*retries=*/3);
    }
    settings_to_write = new_settings_defaults;
    if (sm == SettingsMode::MIGRATE && old_settings_page.size() == settings_to_write.size()) {
      settings_to_write = new_settings_defaults;
      std::copy(old_settings_page.begin(), old_settings_page.end(), settings_to_write.begin());
      if (settings_to_write.size() >= 2 && new_settings_defaults.size() >= 2) {
        settings_to_write[0] = new_settings_defaults[0];
        settings_to_write[1] = new_settings_defaults[1];
      }
    }
  }

  // -------- Sparse erase ----------
  auto nonff_in = [&](uint32_t start, uint32_t end){
    uint32_t s = std::max(start, (uint32_t)m->firmware_start);
    uint32_t e = std::min(end,   (uint32_t)m->bootloader_address);
    for (uint32_t a=s; a<e; ++a) if (image[a - m->firmware_start] != 0xFF) return true;
    return false;
  };

  auto overlaps = [](uint32_t s1, uint32_t e1, uint32_t s2, uint32_t e2){ return !(e1<=s2 || e2<=s1); };
  auto is_soft_protected_page = [&](uint32_t page_start, uint32_t page_end)->bool{
    if (erase_eeprom) return false;
    return overlaps(page_start, page_end, eeprom_start, eeprom_end_excl);
  };

  const bool do_full_erase = full_erase_app || full_erase_entire_app;
  uint32_t erase_end_excl = app_end_excl;
  if (do_full_erase && !full_erase_entire_app) {
    std::optional<uint32_t> highest_nonff;
    for (uint32_t a=app_start; a<app_end_excl; ++a) {
      if (!erase_eeprom && a>=eeprom_start && a<eeprom_end_excl) continue;
      if (image[a - m->firmware_start] != 0xFF) highest_nonff = a;
    }

    if (!highest_nonff.has_value()) {
      erase_end_excl = std::min<uint32_t>(app_start + (uint32_t)m->page_size, app_end_excl);
    } else {
      erase_end_excl = align_up(highest_nonff.value() + 1u, (uint32_t)m->page_size);
      erase_end_excl = std::min<uint32_t>(erase_end_excl, app_end_excl);
    }

    auto fmt16 = [&](uint32_t v){ char b[8]; snprintf(b,8,"%04X", (uint16_t)(v & 0xFFFF)); return std::string(b); };
    log_.info("Full erase range: [0x"+fmt16(app_start)+" .. 0x"+fmt16(erase_end_excl)+") (computed from HEX footprint)");
  }

  std::vector<std::pair<uint32_t,uint16_t>> pages;
  {
    int page_mult = (m->page_size!=512)? 4 : 1;
    uint32_t loop_end = (uint32_t)m->bootloader_address;
    if (do_full_erase && !full_erase_entire_app) loop_end = erase_end_excl;
    for (uint32_t addr=m->firmware_start; addr<loop_end; addr += m->page_size){
      uint32_t end  = std::min<uint32_t>(addr + m->page_size, loop_end);
      if (is_soft_protected_page(addr, end)) continue;
      if (do_full_erase || nonff_in(addr, end)) {
        uint32_t idx_page32 = (addr / (uint32_t)m->page_size) * (uint32_t)page_mult;
        if (idx_page32 > 0xFFFFu) {
          log_.info("Warning: page idx overflow (" + std::to_string(idx_page32) + ")");
          idx_page32 = 0xFFFFu;
        }
        pages.emplace_back(addr, (uint16_t)idx_page32);
      }
    }
  }

  if (pages.empty()){
    log_.info("No app pages contain data (image is all 0xFF in app range) — skipping erase/program/verify.");
    if (sm != SettingsMode::PRESERVE && settings_to_write.size() == (size_t)m->page_size) {
      SelectionReport srep;
      (void)fw_.select_target_session(idx,
                                      mapping_mode,
                                      /*allow_alt_addresses=*/false,
                                      /*direct_attempts=*/std::max(6, probe_tries),
                                      /*index_attempts=*/std::max(3, probe_tries),
                                      &srep);

      int page_mult = (m->page_size!=512)? 4 : 1;
      uint32_t idx_page32 = ((uint32_t)eeprom_start / (uint32_t)m->page_size) * (uint32_t)page_mult;
      if (idx_page32 > 0xFFFFu) idx_page32 = 0xFFFFu;
      uint16_t idx_page = (uint16_t)idx_page32;
      if (!dry_run) {
        log_.info("Erasing settings page @0x" + [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)eeprom_start); return std::string(b);}() + " (page idx "+std::to_string(idx_page)+")");
        if (!fw_.erase_page(idx_page)) log_.info("Warning: settings page erase reported failure");
        for (uint32_t off=0; off<(uint32_t)settings_to_write.size(); ){
          uint32_t chunk_len = std::min<uint32_t>(256u, (uint32_t)settings_to_write.size() - off);
          std::vector<uint8_t> chunk(settings_to_write.begin() + (ptrdiff_t)off, settings_to_write.begin() + (ptrdiff_t)(off + chunk_len));
          if (!fw_.write((uint16_t)(eeprom_start+off), chunk)) {
            log_.info("Warning: settings page write reported failure");
            break;
          }
          off += chunk_len;
        }
      } else {
        log_.info("DRY RUN: would update settings page");
      }
    }
    fw_.reset_esc(idx); std::this_thread::sleep_for(std::chrono::milliseconds(20)); log_.info("ESC" + std::to_string(idx+1) + " reset");
    return true;
  }

  if (dry_run){
    size_t bytes_to_program = 0;
    for (auto b : image) if (b != 0xFF) ++bytes_to_program;
    log_.info("DRY RUN: would erase " + std::to_string(pages.size()) + " pages and program " + std::to_string(bytes_to_program) + " bytes");
    if (is_bb51){
      log_.info("DRY RUN: BB51 protected ranges: boot=[0x" + [&](){ char b[8]; snprintf(b,8,"%04X", m->bootloader_address); return std::string(b);}() + "..0x" + [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)flash_end_excl); return std::string(b);}() + ") lock=0x" + [&](){ char b[8]; snprintf(b,8,"%04X", m->lockbyte_address); return std::string(b);}() + " sig=0x" + [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)(m->lockbyte_address-1)); return std::string(b);}());
    }
    fw_.reset_esc(idx); std::this_thread::sleep_for(std::chrono::milliseconds(20)); log_.info("ESC" + std::to_string(idx+1) + " reset");
    return true;
  }

  log_.info("Erasing firmware pages... (" + std::to_string(pages.size()) + " pages)");
  int erased=0;
  for (auto& p : pages){
    bool page_ok = false;
    for (int etry = 0; etry < std::max(1, erase_retries); ++etry) {
      if (fw_.erase_page(p.second)) { page_ok = true; break; }
      if (etry + 1 < erase_retries) {
        log_.info("Erase retry " + std::to_string(etry + 2) + "/" + std::to_string(erase_retries) +
                  " for page @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)p.first); return std::string(b); })());
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, erase_inter_page_ms)));
        SelectionReport erep;
        (void)fw_.select_target_session(idx, mapping_mode, /*allow_alt_addresses=*/false,
                                        /*direct_attempts=*/std::max(6, probe_tries),
                                        /*index_attempts=*/std::max(3, probe_tries),
                                        &erep);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
      }
    }
    if (!page_ok) {
      last_error_ = "erase failed at page @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)p.first); return std::string(b); })();
      log_.info("Erase failed at page idx "+std::to_string(p.second)+" (addr 0x"+ [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)p.first); return std::string(b);}() + ") after " + std::to_string(erase_retries) + " tries");
      return false;
    }
    if (erase_inter_page_ms > 0 && erased > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(erase_inter_page_ms));
    }
    if (++erased % 8 == 0) log_.info("  erased "+std::to_string(erased)+" pages...");
  }
  log_.info("Erase done: "+std::to_string(erased)+" pages");

  // -------- Program with adaptive writes, retries + recovery -----------------
  log_.info("Programming...");
  const int max_write_tries = std::max(1, write_retries);
  auto try_write = [&](uint16_t a, const std::vector<uint8_t>& blk) -> bool {
    for (int attempt = 0; attempt < max_write_tries; ++attempt) {
      if (fw_.write(a, blk)) return true;
      if (attempt + 1 < max_write_tries) {
        int backoff = std::max(10, write_inter_block_ms) * (attempt + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        SelectionReport wrep;
        (void)fw_.select_target_session(idx, mapping_mode, /*allow_alt_addresses=*/false,
                                        /*direct_attempts=*/std::max(6, probe_tries),
                                        /*index_attempts=*/std::max(3, probe_tries),
                                        &wrep);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    return false;
  };

  const size_t kSizesSafe[] = {128,64};
  const size_t kSizesFast[] = {256,128,64};
  size_t written=0;
  for (size_t off=0; off<image.size(); ){
    bool okblk=false;
    const size_t* sizes = safe ? kSizesSafe : kSizesFast;
    const size_t sizes_n = safe ? (sizeof(kSizesSafe)/sizeof(kSizesSafe[0])) : (sizeof(kSizesFast)/sizeof(kSizesFast[0]));
    for (size_t i=0; i<sizes_n; ++i){
      size_t s = sizes[i];
      size_t n = std::min(s, image.size()-off);
      std::vector<uint8_t> blk(image.begin()+off, image.begin()+off+n);

      if (std::all_of(blk.begin(), blk.end(), [](uint8_t b){ return b==0xFF; })){
        off += n; okblk=true; break;
      }

      uint16_t addr = (uint16_t)(m->firmware_start + off);
      if (try_write(addr, blk)){
        off += n; written += n; okblk=true;
        if ((written/256)%32==0) log_.info("  "+std::to_string(written)+"/"+std::to_string(image.size())+" bytes");
        if (write_inter_block_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(write_inter_block_ms));
        break;
      }
    }

    if (!okblk){
      uint16_t addr = (uint16_t)(m->firmware_start + off);
      last_error_ = "write failed @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", addr); return std::string(b); })();
      log_.info("Write failed repeatedly @0x" + [&](){ char b[8]; snprintf(b,8,"%04X", addr); return std::string(b);}() + " after " + std::to_string(max_write_tries) + " tries");
      return false;
    }
  }
  log_.info("Programming complete");

  if (sm != SettingsMode::PRESERVE) {
    SelectionReport srep;
    (void)fw_.select_target_session(idx,
                                    mapping_mode,
                                    /*allow_alt_addresses=*/false,
                                    /*direct_attempts=*/std::max(6, probe_tries),
                                    /*index_attempts=*/std::max(3, probe_tries),
                                    &srep);

    auto erase_settings_page = [&]()->bool{
      int page_mult = (m->page_size!=512)? 4 : 1;
      uint32_t idx_page32 = ((uint32_t)eeprom_start / (uint32_t)m->page_size) * (uint32_t)page_mult;
      if (idx_page32 > 0xFFFFu) idx_page32 = 0xFFFFu;
      uint16_t idx_page = (uint16_t)idx_page32;
      log_.info("Erasing settings page @0x" + [&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)eeprom_start); return std::string(b);}() + " (page idx "+std::to_string(idx_page)+")");
      return fw_.erase_page(idx_page);
    };

    auto write_flash_range = [&](uint32_t addr, const std::vector<uint8_t>& buf, int retries)->bool{
      for (uint32_t off=0; off<(uint32_t)buf.size(); ){
        uint32_t chunk_len = std::min<uint32_t>(256u, (uint32_t)buf.size() - off);
        std::vector<uint8_t> chunk(buf.begin() + (ptrdiff_t)off, buf.begin() + (ptrdiff_t)(off + chunk_len));
        bool ok=false;
        for (int t=0; t<std::max(1,retries); ++t){
          if (fw_.write((uint16_t)(addr+off), chunk)) { ok=true; break; }
          SelectionReport rrep;
          (void)fw_.select_target_session(idx, mapping_mode, /*allow_alt_addresses=*/false,
                                          /*direct_attempts=*/std::max(6, probe_tries),
                                          /*index_attempts=*/std::max(3, probe_tries),
                                          &rrep);
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!ok) return false;
        off += chunk_len;
      }
      return true;
    };

    if (settings_to_write.size() != (size_t)m->page_size) {
      log_.info("Warning: could not build settings page from HEX; skipping settings update");
    } else {
      if (!dry_run) {
        if (!erase_settings_page()) log_.info("Warning: settings page erase reported failure");
        log_.info(std::string("Writing settings page (") + (sm==SettingsMode::ERASE? "erase" : "migrate") + ")...");
        if (!write_flash_range(eeprom_start, settings_to_write, /*retries=*/3)) {
          log_.info("Warning: settings page write reported failure");
        }

        auto rb = read_flash_range(eeprom_start, std::min<uint32_t>((uint32_t)m->page_size, 256u), /*retries=*/3);
        if (!rb.empty() && rb.size()>=2) {
          int mv = (int)rb[0];
          int sv = (int)rb[1];
          std::string freq = "--";
          if (rb.size() > (size_t)OFF_PWM_FREQ) {
            if (auto f = extract_pwm_khz(rb)) freq = std::to_string(*f) + "kHz";
          }
          log_.info("Settings readback: v" + std::to_string(mv) + "." + std::to_string(sv) + " FREQ=" + freq);
        } else {
          log_.info("Warning: settings readback failed");
        }
      } else {
        log_.info("DRY RUN: would update settings page");
      }
    }
  }

  {
    auto defaults = ih.build(eeprom_start, eeprom_end_excl);
    int exp_mv = defaults.size()>=2? (int)defaults[0] : -1;
    int exp_sv = defaults.size()>=2? (int)defaults[1] : -1;
    std::string exp_freq = "--";
    if (!defaults.empty()) {
      if (auto f = extract_pwm_khz(defaults)) exp_freq = std::to_string(*f) + "kHz";
    }

    SelectionReport srep;
    (void)fw_.select_target_session(idx,
                                    mapping_mode,
                                    /*allow_alt_addresses=*/false,
                                    /*direct_attempts=*/std::max(6, probe_tries),
                                    /*index_attempts=*/std::max(3, probe_tries),
                                    &srep);
    auto rb = read_flash_range(eeprom_start, std::min<uint32_t>((uint32_t)m->page_size, 256u), /*retries=*/3);
    if (!rb.empty() && rb.size()>=2) {
      int mv = (int)rb[0];
      int sv = (int)rb[1];
      std::string freq = "--";
      if (auto f = extract_pwm_khz(rb)) freq = std::to_string(*f) + "kHz";
      if (exp_mv >= 0) {
        log_.info("Settings expected (HEX defaults): v" + std::to_string(exp_mv) + "." + std::to_string(exp_sv) + " FREQ=" + exp_freq);
        log_.info("Settings actual: v" + std::to_string(mv) + "." + std::to_string(sv) + " FREQ=" + freq);
        if (sm == SettingsMode::PRESERVE && (mv != exp_mv || sv != exp_sv || freq != exp_freq)) {
          log_.info("Warning: firmware programmed; settings preserved so configurators may still show old version/frequency (use --settings migrate or --settings erase)");
        }
      }
    }
  }

  // ---------------------------- Verify ---------------------------------------
  if (verify_mode == VerifyMode::FULL){
    if (verify_all_bytes) {
      log_.info("Verifying (full, all bytes)...");
      if (!(full_erase_app || full_erase_entire_app)) {
        log_.info("Warning: --verify-all-bytes without --full-erase-app may fail on untouched pages (old bytes may legitimately remain)");
      }
    } else {
      log_.info("Verifying (full)...");
    }

    auto snippet = [](const uint8_t* p, size_t len){
      size_t L = std::min<size_t>(8, len);
      std::string s; char b[4];
      for (size_t i=0;i<L;++i){ if(i) s.push_back(' '); snprintf(b,4,"%02X", p[i]); s += b; }
      return s;
    };

    const size_t verify_chunk = safe ? 128u : 256u;

    auto verify_range = [&](uint32_t start, uint32_t end)->bool{
      if (end <= start) return true;

      auto compare_chunk = [&](uint32_t chunk_start, const std::vector<uint8_t>& got, size_t n)->std::optional<size_t>{
        size_t off = (size_t)(chunk_start - app_start);
        auto first_mismatch = [&](size_t i0, size_t i1)->std::optional<size_t>{
          for (size_t i=i0; i<i1; ++i) {
            if (got[i] != image[off + i]) return i;
          }
          return std::nullopt;
        };

        const bool verify_settings_bytes = (sm == SettingsMode::ERASE);
        if (verify_settings_bytes) {
          return first_mismatch(0, n);
        }

        uint32_t chunk_end_excl = chunk_start + (uint32_t)n;
        uint32_t ovl_start = std::max<uint32_t>(chunk_start, (uint32_t)eeprom_start);
        uint32_t ovl_end_excl = std::min<uint32_t>(chunk_end_excl, (uint32_t)eeprom_end_excl);

        if (ovl_start >= ovl_end_excl) {
          return first_mismatch(0, n);
        }

        size_t pre_len = (size_t)(ovl_start - chunk_start);
        size_t post_start = (size_t)(ovl_end_excl - chunk_start);
        std::optional<size_t> bad;
        if (pre_len > 0) bad = first_mismatch(0, pre_len);
        if (!bad && post_start < n) bad = first_mismatch(post_start, n);
        return bad;
      };

      auto log_mismatch = [&](uint16_t addr, size_t bad, uint32_t chunk_start, size_t n, const std::vector<uint8_t>& got){
        size_t off = (size_t)(chunk_start - app_start);
        char abuf[8]; snprintf(abuf,8,"%04X", addr);
        log_.info(std::string("Verify mismatch @0x") + abuf + " +" + std::to_string(bad) +
                  " expected: " + snippet(&image[off+bad], n-bad) +
                  " got: "      + snippet(got.data()+bad, n-bad));
      };

      auto try_read_once = [&](uint16_t addr, size_t n)->std::vector<uint8_t>{
        return fw_.read(addr, (int)n, /*retries=*/1);
      };

      auto read_with_recovery = [&](uint16_t addr, size_t n)->std::vector<uint8_t>{
        const int kTries = 3;
        for (int attempt=0; attempt<kTries; ++attempt){
          auto got = try_read_once(addr, n);
          if (!got.empty()) return got;
          if (log_.want(LogLevel::TRACE) && (attempt + 1) < kTries) {
            log_.trace("Verify read failed @0x" + [&](){ char b[8]; snprintf(b,8,"%04X", addr); return std::string(b);}() + "; retrying (try " + std::to_string(attempt+2) + "/" + std::to_string(kTries) + ")");
          }
        }

        if (log_.want(LogLevel::TRACE)) log_.trace("Reselecting target and retrying verify read...");
        SelectionReport rrep;
        (void)fw_.select_target_session(idx,
                                        mapping_mode,
                                        /*allow_alt_addresses=*/false,
                                        /*direct_attempts=*/std::max(6, probe_tries),
                                        /*index_attempts=*/std::max(3, probe_tries),
                                        &rrep);
        for (int attempt=0; attempt<2; ++attempt){
          auto got = try_read_once(addr, n);
          if (!got.empty()) return got;
        }
        return {};
      };

      std::function<bool(uint32_t,size_t,size_t)> verify_segment;
      verify_segment = [&](uint32_t a, size_t n, size_t chunk_size)->bool{
        uint16_t addr = (uint16_t)a;
        auto got = read_with_recovery(addr, n);
        if (!got.empty()) {
          if (got.size()!=n) {
            last_error_ = "Verify read failed @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", addr); return std::string(b); })();
            log_.info(last_error_);
            return false;
          }
          if (auto bad = compare_chunk(a, got, n)) { log_mismatch(addr, *bad, a, n, got); last_error_ = "Verify mismatch @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", addr); return std::string(b); })(); return false; }
          return true;
        }

        if (chunk_size > 64u) {
          size_t next_chunk = std::max<size_t>(64u, chunk_size/2u);
          if (log_.want(LogLevel::TRACE)) {
            log_.trace("Falling back to smaller verify chunk: " + std::to_string(chunk_size) + " -> " + std::to_string(next_chunk));
          }
          for (size_t off=0; off<n; off += next_chunk){
            size_t part_n = std::min(next_chunk, n-off);
            if (!verify_segment(a + (uint32_t)off, part_n, next_chunk)) return false;
          }
          return true;
        }

        last_error_ = "Verify read failed @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", addr); return std::string(b); })();
        log_.info(last_error_);
        return false;
      };

      for (uint32_t a=start; a<end; a += (uint32_t)verify_chunk){
        size_t n = (size_t)std::min<uint32_t>((uint32_t)verify_chunk, end - a);
        if (!verify_segment(a, n, verify_chunk)) return false;
      }
      return true;
    };

    if (verify_all_bytes) {
      if (!verify_range(app_start, app_end_excl)) return false;
    } else {
      for (auto& p : pages){
        uint32_t start = p.first;
        uint32_t end   = std::min<uint32_t>(start + m->page_size, m->bootloader_address);
        if (!verify_range(start, end)) return false;
      }
    }

    log_.info("Verify OK");
  } else if (verify_mode == VerifyMode::FAST){
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    log_.info("Verifying (fast, page CRC)...");

    auto fast_verify_read = [&](uint16_t addr, size_t want) -> std::vector<uint8_t> {
      std::vector<size_t> chunk_sizes;
      if (want > 128) chunk_sizes.push_back(want);
      if (want > 64)  chunk_sizes.push_back(128);
      chunk_sizes.push_back(64);

      for (size_t cs : chunk_sizes) {
        std::vector<uint8_t> result;
        result.reserve(want);
        bool chunk_ok = true;

        for (size_t off = 0; off < want && chunk_ok; off += cs) {
          size_t n = std::min(cs, want - off);
          bool got_it = false;

          for (int attempt = 0; attempt < std::max(2, verify_read_retries) && !got_it; ++attempt) {
            auto chunk = fw_.read((uint16_t)(addr + off), (int)n, /*retries=*/2);
            if (chunk.size() == n) {
              result.insert(result.end(), chunk.begin(), chunk.end());
              got_it = true;
            } else {
              fw_.reset_esc(idx);
              int backoff = 30 * (attempt + 1);
              std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
              SelectionReport rrep;
              (void)fw_.select_target_session(idx, mapping_mode,
                                              /*allow_alt_addresses=*/false,
                                              /*direct_attempts=*/std::max(6, probe_tries),
                                              /*index_attempts=*/std::max(3, probe_tries),
                                              &rrep);
              std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
          }

          if (!got_it) { chunk_ok = false; }
        }

        if (chunk_ok && result.size() == want) return result;

        // Chunk size failed entirely; try smaller
        if (cs > chunk_sizes.back()) {
          fw_.reset_esc(idx);
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          SelectionReport rrep;
          (void)fw_.select_target_session(idx, mapping_mode,
                                          /*allow_alt_addresses=*/false,
                                          /*direct_attempts=*/std::max(6, probe_tries),
                                          /*index_attempts=*/std::max(3, probe_tries),
                                          &rrep);
          std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
      }
      return {};  // all chunk sizes exhausted
    };

    for (auto& p : pages){
      uint32_t start = p.first;
      uint32_t end   = std::min<uint32_t>(start + m->page_size, m->bootloader_address);

      std::vector<uint8_t> expect(end - start, 0xFF);
      for (uint32_t a=start; a<end; ++a) expect[a-start] = image[a - m->firmware_start];
      uint32_t crc_expect = crc32(expect.data(), expect.size());

      // Read using cap of 128 bytes per chunk (avoid brittle 256-byte reads)
      std::vector<uint8_t> got; got.reserve(expect.size());
      for (uint32_t a=start; a<end; a += 128){
        size_t n = (size_t)std::min<uint32_t>(128, end - a);
        auto chunk = fast_verify_read((uint16_t)a, n);
        if (chunk.size() != n){
          last_error_ = "Verify read failed @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)a); return std::string(b); })();
          log_.info(last_error_);
          return false;
        }
        got.insert(got.end(), chunk.begin(), chunk.end());
      }
      uint32_t crc_got = crc32(got.data(), got.size());
      if (crc_got != crc_expect){
        last_error_ = "Verify CRC mismatch on page @0x" + ([&](){ char b[8]; snprintf(b,8,"%04X", (uint16_t)start); return std::string(b); })();
        log_.info(last_error_);
        return false;
      }
    }
    log_.info("Verify OK (fast)");
  } else {
    log_.info("Verify: off");
  }

  fw_.reset_esc(idx); std::this_thread::sleep_for(std::chrono::milliseconds(20)); log_.info("ESC" + std::to_string(idx+1) + " reset");
  return true;
}

bool Flasher::read_settings_full(int idx, ReadRowFull* out, bool allow_alt_addressing) {
  if (out) *out = ReadRowFull{};
  if (out) out->esc = idx + 1;

  for (int parse_attempt = 0; parse_attempt < 2; ++parse_attempt) {
    SelectionReport rep;
    std::vector<uint8_t> data;

    if (!read_block_with_recovery(idx, /*base=*/0, BJ::EEPROM_SIZE, /*chunk_max=*/128, &data, &rep, allow_alt_addressing)) {
      if (out) {
        out->sig = rep.sig;
        out->select_attempts = rep.attempts;
        out->mapping_used = !fw_.last_mapping_used().empty() ? fw_.last_mapping_used() : (rep.used_direct ? std::string("direct") : std::string());
        out->error = "read failed";
      }
      return false;
    }

    BluejayRawEeprom raw{};
    std::copy(data.begin(), data.begin() + BJ::EEPROM_SIZE, raw.bytes.begin());

    BluejayIdentity id{};
    BluejaySettings s{};
    BluejaySettingsDisplay d{};
    if (!bj_parse_identity(raw, &id) || !bj_parse_settings(raw, id, &s) || !bj_make_display(s, id, &d)) {
      fw_.reset_esc(idx);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    if (out) {
      out->sig = rep.sig;
      out->select_attempts = rep.attempts;
      out->mapping_used = !fw_.last_mapping_used().empty() ? fw_.last_mapping_used() : (rep.used_direct ? std::string("direct") : std::string());
      out->raw_page = data;
      out->identity = id;
      out->settings = s;
      out->display = d;
      out->ok = true;
    }
    return true;
  }

  if (out) out->error = "parse failed";
  return false;
}

bool Flasher::read_settings_page(int idx, uint16_t base, size_t len, std::vector<uint8_t>* out) {
  SelectionReport rep;
  std::vector<uint8_t> data;
  if (!read_block_with_recovery(idx, base, len, /*chunk_max=*/256, &data, &rep)) {
    log_.info("read_settings_page: read failed for ESC" + std::to_string(idx + 1));
    return false;
  }
  if (out) *out = std::move(data);
  return true;
}

bool Flasher::write_settings_page(int idx, uint16_t base, const std::vector<uint8_t>& page) {
  SelectionReport rep;
  bool ok = select_settings_target(idx, /*index_tries=*/std::max(2, probe_tries), /*direct_tries=*/2, &rep);
  if (!ok) {
    log_.info("write_settings_page: select failed for ESC" + std::to_string(idx + 1));
    return false;
  }

  uint16_t sig = rep.sig.value_or(0);
  const SilabsMcu* m = silabs_from_sig(sig);
  if (!m) {
    log_.info("write_settings_page: unknown MCU signature 0x" + [&](){ char b[8]; snprintf(b, 8, "%04X", sig); return std::string(b); }());
    return false;
  }

  // Compute page index for erase
  uint32_t page_mult = (m->page_size != 512) ? 4u : 1u;
  uint32_t idx_page32 = ((uint32_t)base / (uint32_t)m->page_size) * page_mult;
  if (idx_page32 > 0xFFFFu) idx_page32 = 0xFFFFu;
  uint16_t idx_page = (uint16_t)idx_page32;

  log_.info("Erasing settings page @0x" + [&](){ char b[8]; snprintf(b, 8, "%04X", base); return std::string(b); }() + " (page idx " + std::to_string(idx_page) + ")");
  if (!fw_.erase_page(idx_page)) {
    log_.info("write_settings_page: erase failed");
    return false;
  }

  // Write in 256-byte chunks
  log_.info("Writing settings page (" + std::to_string(page.size()) + " bytes)...");
  for (size_t off = 0; off < page.size(); off += 256) {
    size_t chunk_len = std::min<size_t>(256, page.size() - off);
    std::vector<uint8_t> chunk(page.begin() + (ptrdiff_t)off, page.begin() + (ptrdiff_t)(off + chunk_len));

    bool wrote = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (fw_.write((uint16_t)(base + off), chunk)) {
        wrote = true;
        break;
      }
      fw_.reset_esc(idx);
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      SelectionReport srep;
      (void)select_settings_target(idx, /*index_tries=*/std::max(2, probe_tries), /*direct_tries=*/2, &srep);
    }
    if (!wrote) {
      log_.info("write_settings_page: write failed @0x" + [&](){ char b[8]; snprintf(b, 8, "%04X", (uint16_t)(base + off)); return std::string(b); }());
      return false;
    }
  }

  log_.info("Settings page written");
  return true;
}

bool Flasher::update_settings(int idx, const SettingsPatch& patch, BluejaySettingsDisplay* after) {
  log_.info("\n=== ESC" + std::to_string(idx + 1) + ": updating settings ===");

  // 1) Select and get signature
  SelectionReport rep;
  bool ok = select_settings_target(idx, /*index_tries=*/std::max(2, probe_tries), /*direct_tries=*/2, &rep);
  if (!ok) {
    log_.info("update_settings: select failed");
    return false;
  }

  uint16_t sig = rep.sig.value_or(0);
  const SilabsMcu* m = silabs_from_sig(sig);
  if (!m) {
    log_.info("update_settings: unknown MCU signature");
    return false;
  }

  uint16_t base = eeprom_base_for(sig);
  size_t page_size = (size_t)m->page_size;

  // 2) Read full settings page
  std::vector<uint8_t> page;
  if (!read_settings_page(idx, base, page_size, &page)) {
    log_.info("update_settings: failed to read settings page");
    return false;
  }

  // 3) Parse identity to check layout version
  BluejayRawEeprom raw{};
  std::copy(page.begin(), page.begin() + std::min(page.size(), (size_t)BJ::EEPROM_SIZE), raw.bytes.begin());
  BluejayIdentity id{};
  if (!bj_parse_identity(raw, &id)) {
    log_.info("update_settings: failed to parse identity");
    return false;
  }
  log_.info("Current: Bluejay v" + std::to_string(id.fw_main) + "." + std::to_string(id.fw_sub) + " (layout " + std::to_string(id.layout_version) + ")");

  // 4) Apply patch
  if (!bj_apply_patch(page, patch)) {
    log_.info("update_settings: failed to apply patch");
    return false;
  }

  // 5) Write back
  if (!write_settings_page(idx, base, page)) {
    log_.info("update_settings: failed to write settings page");
    return false;
  }

  // 6) Verify by re-reading
  std::vector<uint8_t> verify_page;
  if (!read_settings_page(idx, base, page_size, &verify_page)) {
    log_.info("update_settings: failed to verify (read failed)");
    return false;
  }

  if (verify_page.size() != page.size() || !std::equal(page.begin(), page.end(), verify_page.begin())) {
    log_.info("update_settings: verify mismatch");
    return false;
  }
  log_.info("Settings verified OK");

  // 7) Parse and return updated display
  if (after) {
    BluejayRawEeprom raw2{};
    std::copy(verify_page.begin(), verify_page.begin() + std::min(verify_page.size(), (size_t)BJ::EEPROM_SIZE), raw2.bytes.begin());
    BluejayIdentity id2{};
    BluejaySettings s2{};
    bj_parse_identity(raw2, &id2);
    bj_parse_settings(raw2, id2, &s2);
    bj_make_display(s2, id2, after);
  }

  fw_.reset_esc(idx);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  log_.info("ESC" + std::to_string(idx+1) + " reset");
  return true;
}
