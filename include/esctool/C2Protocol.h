#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace esctool {

namespace C2Action {
    constexpr uint8_t ACK   = 0x00;
    constexpr uint8_t INIT  = 0x01;
    constexpr uint8_t RESET = 0x02;
    constexpr uint8_t WRITE = 0x03;
    constexpr uint8_t ERASE = 0x04;
    constexpr uint8_t READ  = 0x05;
    constexpr uint8_t INFO  = 0x08;
    constexpr uint8_t PING  = 0x0F;
    
    constexpr uint8_t SUCCESS_MASK = 0x80;
    constexpr uint8_t CRC_ERROR_MASK = 0x40;
}

struct C2DeviceInfo {
    uint8_t device_id = 0;
    uint8_t revision = 0;
    std::string device_id_hex;
    std::string revision_hex;
};

struct C2Options {
    std::string port;
    uint32_t baud = 1000000;
    uint32_t timeout_ms = 2000;
    uint32_t connect_delay_ms = 2000;
};

inline uint8_t c2_calc_crc(uint16_t address, const uint8_t* data, size_t len) {
    uint32_t crc = ((address >> 8) & 0xFF) + (address & 0xFF);
    for (size_t i = 0; i < len; ++i) {
        crc += data[i];
    }
    return static_cast<uint8_t>(crc & 0xFF);
}

inline std::vector<uint8_t> c2_build_ping() {
    return { C2Action::PING, 0x00 };
}

inline std::vector<uint8_t> c2_build_init() {
    return { C2Action::INIT, 0x00 };
}

inline std::vector<uint8_t> c2_build_reset() {
    return { C2Action::RESET, 0x00 };
}

inline std::vector<uint8_t> c2_build_erase() {
    return { C2Action::ERASE, 0x00 };
}

inline std::vector<uint8_t> c2_build_info() {
    return { C2Action::INFO, 0x00 };
}

inline std::vector<uint8_t> c2_build_read(uint32_t address, uint8_t amount) {
    return {
        C2Action::READ,
        C2Action::READ,
        amount,
        static_cast<uint8_t>((address >> 16) & 0xFF),
        static_cast<uint8_t>((address >> 8) & 0xFF),
        static_cast<uint8_t>(address & 0xFF),
        0x00
    };
}

inline std::vector<uint8_t> c2_build_write(uint16_t address, const uint8_t* data, size_t len) {
    uint8_t crc = c2_calc_crc(address, data, len);
    uint8_t totalLen = static_cast<uint8_t>(len + 5);
    
    std::vector<uint8_t> cmd;
    cmd.reserve(7 + len);
    cmd.push_back(C2Action::WRITE);
    cmd.push_back(totalLen);
    cmd.push_back(static_cast<uint8_t>(len));
    cmd.push_back(0x00);
    cmd.push_back(static_cast<uint8_t>((address >> 8) & 0xFF));
    cmd.push_back(static_cast<uint8_t>(address & 0xFF));
    cmd.push_back(crc);
    cmd.insert(cmd.end(), data, data + len);
    return cmd;
}

inline bool c2_is_success(uint8_t action, uint8_t response) {
    return response == (action | C2Action::SUCCESS_MASK);
}

inline bool c2_is_crc_error(uint8_t action, uint8_t response) {
    return response == (action & C2Action::CRC_ERROR_MASK);
}

}
