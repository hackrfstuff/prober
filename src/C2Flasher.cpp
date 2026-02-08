#include "esctool/C2Flasher.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace esctool {

C2Flasher::C2Flasher(ISerial& serial, Log& log)
    : serial_(serial), log_(log) {}

void C2Flasher::clear_buffer() {
    // Read and discard any pending data
    uint8_t buf[256];
    size_t total = 0;
    while (true) {
        size_t n = serial_.read(buf, sizeof(buf));
        if (n == 0) break;
        total += n;
        if (total > 4096) break;  // Safety limit
    }
    if (total > 0) {
        log_.debug("C2: cleared " + std::to_string(total) + " stale bytes");
    }
}

std::vector<uint8_t> C2Flasher::write_and_wait(const std::vector<uint8_t>& cmd, size_t expected_len) {
    std::vector<uint8_t> response;
    response.reserve(expected_len);

    // Write command
    size_t written = serial_.write(cmd.data(), cmd.size());
    if (written != cmd.size()) {
        last_error_ = "Failed to write command";
        return response;
    }

    // Read response with timeout
    auto start = std::chrono::steady_clock::now();
    while (response.size() < expected_len) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms_) {
            last_error_ = "Timeout waiting for response";
            break;
        }

        uint8_t buf[256];
        size_t to_read = std::min(sizeof(buf), expected_len - response.size());
        size_t n = serial_.read(buf, to_read);
        if (n > 0) {
            response.insert(response.end(), buf, buf + n);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    return response;
}

bool C2Flasher::has_interface() {
    clear_buffer();

    auto cmd = c2_build_ping();
    auto resp = write_and_wait(cmd, 1);

    if (resp.empty()) {
        last_error_ = "No response to PING";
        return false;
    }

    bool ok = c2_is_success(C2Action::PING, resp[0]);
    if (!ok) {
        last_error_ = "PING failed (response: 0x" + 
            ([&]{ std::ostringstream ss; ss << std::hex << std::setfill('0') << std::setw(2) << (int)resp[0]; return ss.str(); })() + ")";
    }
    return ok;
}

bool C2Flasher::initialize() {
    clear_buffer();

    auto cmd = c2_build_init();
    auto resp = write_and_wait(cmd, 1);

    if (resp.empty()) {
        last_error_ = "No response to INIT";
        return false;
    }

    bool ok = c2_is_success(C2Action::INIT, resp[0]);
    if (!ok) {
        last_error_ = "INIT failed (response: 0x" +
            ([&]{ std::ostringstream ss; ss << std::hex << std::setfill('0') << std::setw(2) << (int)resp[0]; return ss.str(); })() + ")";
    }
    return ok;
}

bool C2Flasher::reset() {
    clear_buffer();

    auto cmd = c2_build_reset();
    auto resp = write_and_wait(cmd, 1);

    if (resp.empty()) {
        last_error_ = "No response to RESET";
        return false;
    }

    bool ok = c2_is_success(C2Action::RESET, resp[0]);
    if (!ok) {
        last_error_ = "RESET failed";
    }
    return ok;
}

std::optional<C2DeviceInfo> C2Flasher::get_device_info() {
    clear_buffer();

    auto cmd = c2_build_info();
    auto resp = write_and_wait(cmd, 4);

    if (resp.size() != 4) {
        last_error_ = "INFO response too short (got " + std::to_string(resp.size()) + " bytes)";
        return std::nullopt;
    }

    if (!c2_is_success(C2Action::INFO, resp[0])) {
        last_error_ = "INFO failed";
        return std::nullopt;
    }

    C2DeviceInfo info;
    info.device_id = resp[1];
    info.revision = resp[2];

    // Format as hex strings
    {
        std::ostringstream ss;
        ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)info.device_id;
        info.device_id_hex = ss.str();
    }
    {
        std::ostringstream ss;
        ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)info.revision;
        info.revision_hex = ss.str();
    }

    return info;
}

bool C2Flasher::erase() {
    clear_buffer();

    auto cmd = c2_build_erase();
    
    // Erase can take a while, use longer timeout
    uint32_t old_timeout = timeout_ms_;
    timeout_ms_ = std::max(timeout_ms_, 10000u);
    
    auto resp = write_and_wait(cmd, 1);
    
    timeout_ms_ = old_timeout;

    if (resp.empty()) {
        last_error_ = "No response to ERASE";
        return false;
    }

    bool ok = c2_is_success(C2Action::ERASE, resp[0]);
    if (!ok) {
        last_error_ = "ERASE failed";
    }
    return ok;
}

std::vector<uint8_t> C2Flasher::read_flash(uint32_t address, size_t amount) {
    std::vector<uint8_t> result;
    result.reserve(amount);

    const size_t chunk_size = 16;  // Read in 16-byte chunks like the JS version

    for (size_t offset = 0; offset < amount; offset += chunk_size) {
        size_t to_read = std::min(chunk_size, amount - offset);
        uint32_t addr = address + offset;

        clear_buffer();
        auto cmd = c2_build_read(addr, static_cast<uint8_t>(to_read));
        auto resp = write_and_wait(cmd, to_read + 1);

        if (resp.size() < to_read + 1) {
            last_error_ = "READ failed at address 0x" +
                ([&]{ std::ostringstream ss; ss << std::hex << std::uppercase << addr; return ss.str(); })();
            return result;  // Return partial data
        }

        if (!c2_is_success(C2Action::READ, resp[0])) {
            last_error_ = "READ failed at address 0x" +
                ([&]{ std::ostringstream ss; ss << std::hex << std::uppercase << addr; return ss.str(); })();
            return result;
        }

        // Skip status byte, append data
        result.insert(result.end(), resp.begin() + 1, resp.end());
    }

    return result;
}

bool C2Flasher::write_flash(uint16_t address, const uint8_t* data, size_t len) {
    clear_buffer();

    auto cmd = c2_build_write(address, data, len);
    auto resp = write_and_wait(cmd, 1);

    if (resp.empty()) {
        last_error_ = "No response to WRITE at 0x" +
            ([&]{ std::ostringstream ss; ss << std::hex << std::uppercase << address; return ss.str(); })();
        return false;
    }

    if (c2_is_crc_error(C2Action::WRITE, resp[0])) {
        last_error_ = "CRC error at 0x" +
            ([&]{ std::ostringstream ss; ss << std::hex << std::uppercase << address; return ss.str(); })();
        return false;
    }

    if (!c2_is_success(C2Action::WRITE, resp[0])) {
        last_error_ = "WRITE failed at 0x" +
            ([&]{ std::ostringstream ss; ss << std::hex << std::uppercase << address; return ss.str(); })();
        return false;
    }

    return true;
}

bool C2Flasher::write_hex_image(const std::vector<std::pair<uint32_t, uint8_t>>& hex_data,
                                 C2ProgressCallback progress) {
    if (hex_data.empty()) {
        last_error_ = "No data to write";
        return false;
    }

    // Sort by address
    std::vector<std::pair<uint32_t, uint8_t>> sorted_data = hex_data;
    std::sort(sorted_data.begin(), sorted_data.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Group into contiguous chunks
    struct Chunk {
        uint32_t address;
        std::vector<uint8_t> data;
    };
    std::vector<Chunk> chunks;

    Chunk current;
    current.address = sorted_data[0].first;
    current.data.push_back(sorted_data[0].second);

    for (size_t i = 1; i < sorted_data.size(); ++i) {
        uint32_t expected_addr = current.address + current.data.size();
        if (sorted_data[i].first == expected_addr && current.data.size() < write_chunk_size_) {
            current.data.push_back(sorted_data[i].second);
        } else {
            chunks.push_back(std::move(current));
            current.address = sorted_data[i].first;
            current.data.clear();
            current.data.push_back(sorted_data[i].second);
        }
    }
    chunks.push_back(std::move(current));

    // Further split chunks that are too large
    std::vector<Chunk> final_chunks;
    for (auto& c : chunks) {
        size_t offset = 0;
        while (offset < c.data.size()) {
            size_t len = std::min(write_chunk_size_, c.data.size() - offset);
            Chunk fc;
            fc.address = c.address + offset;
            fc.data.assign(c.data.begin() + offset, c.data.begin() + offset + len);
            final_chunks.push_back(std::move(fc));
            offset += len;
        }
    }

    size_t total_bytes = sorted_data.size();
    size_t bytes_done = 0;
    size_t chunks_total = final_chunks.size();

    log_.info("C2: writing " + std::to_string(total_bytes) + " bytes in " + 
              std::to_string(chunks_total) + " chunks");

    for (size_t i = 0; i < final_chunks.size(); ++i) {
        const auto& chunk = final_chunks[i];

        if (chunk.address > 0xFFFF) {
            last_error_ = "Address out of range: 0x" +
                ([&]{ std::ostringstream ss; ss << std::hex << std::uppercase << chunk.address; return ss.str(); })();
            return false;
        }

        if (!write_flash(static_cast<uint16_t>(chunk.address), chunk.data.data(), chunk.data.size())) {
            return false;
        }

        bytes_done += chunk.data.size();

        if (progress) {
            progress(bytes_done, total_bytes, i + 1, chunks_total);
        }
    }

    return true;
}

}
