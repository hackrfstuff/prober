#pragma once
#include "C2Protocol.h"
#include "ISerial.h"
#include "Log.h"
#include <functional>
#include <memory>

namespace esctool {

using C2ProgressCallback = std::function<void(size_t bytes_done, size_t bytes_total, size_t chunk_index, size_t chunks_total)>;

class C2Flasher {
public:
    C2Flasher(ISerial& serial, Log& log);
    ~C2Flasher() = default;

    void set_timeout_ms(uint32_t ms) { timeout_ms_ = ms; }
    void set_connect_delay_ms(uint32_t ms) { connect_delay_ms_ = ms; }
    void set_write_chunk_size(size_t size) { write_chunk_size_ = size; }

    bool has_interface();

    bool initialize();

    bool reset();

    std::optional<C2DeviceInfo> get_device_info();

    bool erase();

    std::vector<uint8_t> read_flash(uint32_t address, size_t amount);

    bool write_flash(uint16_t address, const uint8_t* data, size_t len);

    bool write_hex_image(const std::vector<std::pair<uint32_t, uint8_t>>& hex_data,
                         C2ProgressCallback progress = nullptr);

    const std::string& last_error() const { return last_error_; }

private:
    std::vector<uint8_t> write_and_wait(const std::vector<uint8_t>& cmd, size_t expected_len);

    void clear_buffer();

    ISerial& serial_;
    Log& log_;
    uint32_t timeout_ms_ = 2000;
    uint32_t connect_delay_ms_ = 2000;
    size_t write_chunk_size_ = 128;
    std::string last_error_;
};

}
