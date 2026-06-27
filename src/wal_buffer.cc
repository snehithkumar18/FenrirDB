#include "wal_buffer.h"
#include "logger.h"
#include <cstring>
#include <algorithm>

namespace FenrirDB {

// ======================================================================
// LogRingBuffer Implementation
// ======================================================================

bool LogRingBuffer::write(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size + len > WAL_BUFFER_SIZE) {
        return false; // Buffer overflow
    }

    size_t space_to_end = WAL_BUFFER_SIZE - tail;
    if (len <= space_to_end) {
        std::memcpy(buffer + tail, data, len);
        tail = (tail + len) % WAL_BUFFER_SIZE;
    } else {
        std::memcpy(buffer + tail, data, space_to_end);
        std::memcpy(buffer, data + space_to_end, len - space_to_end);
        tail = len - space_to_end;
    }
    size += len;
    return true;
}

size_t LogRingBuffer::read(uint8_t* dest, size_t max_len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size == 0) return 0;

    size_t read_len = std::min(size, max_len);
    size_t space_to_end = WAL_BUFFER_SIZE - head;

    if (read_len <= space_to_end) {
        std::memcpy(dest, buffer + head, read_len);
        head = (head + read_len) % WAL_BUFFER_SIZE;
    } else {
        std::memcpy(dest, buffer + head, space_to_end);
        std::memcpy(dest + space_to_end, buffer, read_len - space_to_end);
        head = read_len - space_to_end;
    }
    size -= read_len;
    return read_len;
}

size_t LogRingBuffer::get_size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return size;
}

// ======================================================================
// AsyncLogManager Implementation
// ======================================================================

AsyncLogManager::AsyncLogManager(const std::string& filename) : log_filename(filename) {
    log_file.open(log_filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!log_file.is_open()) {
        log_file.open(log_filename, std::ios::out | std::ios::binary | std::ios::trunc);
        log_file.close();
        log_file.open(log_filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }

    running = true;
    flusher_thread = std::thread(&AsyncLogManager::flusher_loop, this);
    Logger::get_instance().info("AsyncLog", "Async Log Manager started with flusher thread.");
}

AsyncLogManager::~AsyncLogManager() {
    shutdown();
}

uint64_t AsyncLogManager::append_record(uint32_t tx_id, LogRecordType type, uint32_t page_id, uint16_t slot_id,
                                       const std::vector<uint8_t>& before, const std::vector<uint8_t>& after) {
    LogRecord rec;
    rec.lsn = next_lsn++;
    rec.tx_id = tx_id;
    rec.type = type;
    rec.page_id = page_id;
    rec.slot_id = slot_id;
    rec.before_image = before;
    rec.after_image = after;

    std::vector<uint8_t> bytes = rec.serialize();
    
    // Retry writing to ring buffer if full
    while (!ring_buffer.write(bytes.data(), bytes.size())) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    // Wake up the flusher thread
    flush_cv.notify_one();
    return rec.lsn;
}

void AsyncLogManager::flush(uint64_t lsn) {
    std::unique_lock<std::mutex> lock(flush_mutex);
    // Block until the requested LSN has been flushed to disk
    flush_cv.wait(lock, [&]() {
        return flushed_lsn >= lsn;
    });
}

void AsyncLogManager::flusher_loop() {
    uint8_t temp_buffer[1024 * 64]; // 64KB flush buffer
    while (running) {
        std::unique_lock<std::mutex> lock(flush_mutex);
        flush_cv.wait_for(lock, std::chrono::milliseconds(10), [&]() {
            return ring_buffer.get_size() > 0 || !running;
        });

        size_t size_to_read = ring_buffer.get_size();
        if (size_to_read > 0) {
            size_t bytes_read = ring_buffer.read(temp_buffer, sizeof(temp_buffer));
            if (bytes_read > 0 && log_file.is_open()) {
                log_file.write(reinterpret_cast<const char*>(temp_buffer), bytes_read);
                log_file.flush();
                
                // Group Commit LSN update
                flushed_lsn = next_lsn - 1;
                flush_cv.notify_all(); // Wake up any waiting commit threads
            }
        }
    }
}

void AsyncLogManager::shutdown() {
    if (running) {
        running = false;
        flush_cv.notify_all();
        if (flusher_thread.joinable()) {
            flusher_thread.join();
        }
        if (log_file.is_open()) {
            log_file.close();
        }
        Logger::get_instance().info("AsyncLog", "Async Log Manager shutdown complete.");
    }
}

} // namespace FenrirDB
