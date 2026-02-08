#include "esctool/MSP.h"
#include "esctool/Utils.h"
#include <chrono>
#include <stdexcept>

using namespace esctool;

uint8_t MSP::checksum(uint8_t ln, uint8_t cmd, const std::vector<uint8_t>& p){
  uint8_t c = (ln ^ cmd) & 0xFF; for (auto b : p) c ^= b; return c;
}

void MSP::send(uint8_t cmd, const std::vector<uint8_t>& payload){
  std::vector<uint8_t> frm; frm.reserve(3+2+payload.size()+1);
  frm.push_back('$'); frm.push_back('M'); frm.push_back('<');
  frm.push_back((uint8_t)payload.size()); frm.push_back(cmd);
  frm.insert(frm.end(), payload.begin(), payload.end());
  frm.push_back(checksum((uint8_t)payload.size(), cmd, payload));
  ser_.write(frm.data(), frm.size()); ser_.flush();
  log_.debug("MSP TX cmd=" + std::to_string(cmd) + " len=" + std::to_string(payload.size()) + " :: " + hxd(frm));
}

std::vector<uint8_t> MSP::recv(int expect_cmd){
  auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s_);
  auto rd = [&](size_t n){ std::vector<uint8_t> buf(n); size_t got=0; while(got<n && std::chrono::steady_clock::now()<deadline){ got += ser_.read(buf.data()+got, n-got);} buf.resize(got); return buf; };
  // seek '$'
  for(;;){ auto b=rd(1); if(b.empty()) throw std::runtime_error("MSP timeout (no '$')"); if(b[0]=='$') break; }
  auto hdr = rd(2); if(hdr.size()!=2) throw std::runtime_error("MSP bad header");
  if(!(hdr[0]=='M' && (hdr[1]=='>' || hdr[1]=='!'))) throw std::runtime_error("MSP bad header");
  auto ln_b = rd(1); auto cmd_b = rd(1); if(ln_b.size()!=1||cmd_b.size()!=1) throw std::runtime_error("MSP short");
  uint8_t ln=ln_b[0], cmd=cmd_b[0];
  auto pl = rd(ln); if(pl.size()!=ln) throw std::runtime_error("MSP payload timeout");
  auto cs = rd(1); if(cs.size()!=1) throw std::runtime_error("MSP checksum timeout");
  log_.debug("MSP RX cmd="+std::to_string(cmd)+" len="+std::to_string(ln));
  if (checksum(ln,cmd,pl) != cs[0]) throw std::runtime_error("MSP checksum mismatch");
  if (expect_cmd>=0 && cmd != (uint8_t)expect_cmd) throw std::runtime_error("MSP unexpected cmd");
  return pl;
}

std::vector<uint8_t> MSP::req(uint8_t cmd, const std::vector<uint8_t>& payload){ send(cmd,payload); return recv(cmd); }
