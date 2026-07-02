#ifndef FENRIRDB_WAL_BUFFER_H
#define FENRIRDB_WAL_BUFFER_H

#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "wal.h"

namespace FenrirDB {

constexpr size_t WAL_BUFFER_SIZE = 1024 * 1024; // 1MB Ring Buffer

class LogRingBuffer {
private:
    std::vector<uint8_t> buffer;
    size_t head = 0;
    size_t tail = 0;
    size_t size = 0;
    std::mutex mutex_;

public:
    LogRingBuffer() : buffer(WAL_BUFFER_SIZE) {}
    ~LogRingBuffer() = default;

    bool write(const uint8_t* data, size_t len);
    size_t read(uint8_t* dest, size_t max_len);
    size_t get_size();
};

class AsyncLogManager {
private:
    std::string log_filename;
    std::fstream log_file;
    LogRingBuffer ring_buffer;
    
    std::thread flusher_thread;
    std::atomic<bool> running{false};
    std::mutex flush_mutex;
    std::condition_variable flush_cv;
    
    std::atomic<uint64_t> next_lsn{1};
    std::atomic<uint64_t> flushed_lsn{0};

    void flusher_loop();

public:
    explicit AsyncLogManager(const std::string& filename);
    ~AsyncLogManager();

    uint64_t append_record(uint32_t tx_id, LogRecordType type, uint32_t page_id = 0, uint16_t slot_id = 0,
                           const std::vector<uint8_t>& before = {}, const std::vector<uint8_t>& after = {});
    void flush(uint64_t lsn);
    void shutdown();

    uint64_t get_next_lsn() const { return next_lsn; }
    uint64_t get_flushed_lsn() const { return flushed_lsn; }
};

} // namespace FenrirDB

#endif // FENRIRDB_WAL_BUFFER_H
