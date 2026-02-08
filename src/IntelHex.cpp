#include "esctool/IntelHex.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace esctool;

static uint8_t hexbyte(const std::string& s){
  unsigned int v=0; std::stringstream ss; ss<<std::hex<<s; ss>>v; return (uint8_t)v;
}

void IntelHexImage::load(const std::string& path){
  data_.clear(); min_.reset(); max_.reset();
  std::ifstream f(path); if(!f) throw std::runtime_error("Cannot open HEX: "+path);
  std::string line; uint32_t ext=0;
  for (size_t ln=1; std::getline(f,line); ++ln){
    if(line.empty()) continue;
    if(line[0] != ':') throw std::runtime_error("HEX line "+std::to_string(ln)+" missing ':'");
    std::string hex = line.substr(1);
    if(hex.size() < 10) throw std::runtime_error("HEX line "+std::to_string(ln)+" too short");

    // Convert the whole record to bytes
    std::vector<uint8_t> rec; rec.reserve(hex.size()/2);
    for(size_t i=0;i+1<hex.size();i+=2) rec.push_back( hexbyte(hex.substr(i,2)) );

    if (rec.size() < 5) throw std::runtime_error("HEX line "+std::to_string(ln)+" malformed");
    uint8_t  reclen = rec[0];
    if (rec.size() != (size_t)(5 + reclen))
      throw std::runtime_error("HEX line "+std::to_string(ln)+" length mismatch");

    // Validate checksum: sum of all bytes (including checksum) == 0 mod 256
    uint32_t sum = 0; for (auto b: rec) sum += b;
    if ((sum & 0xFF) != 0) throw std::runtime_error("HEX line "+std::to_string(ln)+" bad checksum");

    uint16_t off = (rec[1]<<8)|rec[2];
    uint8_t  rt  = rec[3];

    if (rt==0x00){ // data
      uint32_t addr=(ext<<16)|off;
      for (uint8_t i=0;i<reclen;++i){
        uint32_t a=addr+i; data_[a]=rec[4+i];
        min_=min_? std::min(*min_,a):a; max_=max_? std::max(*max_,a):a;
      }
    } else if (rt==0x01){ // EOF
      break;
    } else if (rt==0x04){ // Extended Linear Address
      if(reclen!=2) throw std::runtime_error("HEX line "+std::to_string(ln)+" bad ELA len");
      ext=(rec[4]<<8)|rec[5];
    } else {
      // ignore other record types
    }
  }
}

std::vector<uint8_t> IntelHexImage::build(uint32_t start, uint32_t end) const{
  if (end<=start) throw std::runtime_error("HEX build: invalid range");
  std::vector<uint8_t> img(end-start, 0xFF);
  for (auto& kv : data_){ if (kv.first>=start && kv.first<end) img[kv.first-start]=kv.second; }
  return img;
}
