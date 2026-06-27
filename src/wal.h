#ifndef FENRIRDB_WAL_H
#define FENRIRDB_WAL_H

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include "errors.h"
#include "storage.h"
#include "cache.h"

namespace FenrirDB {

enum class LogRecordType : uint8_t {
    BEGIN = 0,
    COMMIT = 1,
    ABORT = 2,
    INSERT = 3,
    UPDATE = 4,
    DELETE = 5
};

struct LogRecord {
    uint64_t lsn = 0;
    uint32_t tx_id = 0;
    LogRecordType type = LogRecordType::BEGIN;
    uint32_t page_id = 0;
    uint16_t slot_id = 0;
    std::vector<uint8_t> before_image;
    std::vector<uint8_t> after_image;

    std::vector<uint8_t> serialize() const;
    static LogRecord deserialize(const std::vector<uint8_t>& bytes, size_t& offset);
};

class LogManager {
private:
    std::string log_filename;
    std::fstream log_file;
    uint64_t next_lsn = 1;
    uint64_t flushed_lsn = 0;

public:
    explicit LogManager(const std::string& log_file);
    ~LogManager();

    uint64_t append_record(uint32_t tx_id, LogRecordType type, uint32_t page_id = 0, uint16_t slot_id = 0,
                           const std::vector<uint8_t>& before = {}, const std::vector<uint8_t>& after = {});
    void flush();
    uint64_t get_next_lsn() const { return next_lsn; }
    uint64_t get_flushed_lsn() const { return flushed_lsn; }

    std::vector<LogRecord> read_all_records();
};

class RecoveryManager {
private:
    LogManager& log_manager;
    DiskManager& disk_manager;

public:
    RecoveryManager(LogManager& log_mgr, DiskManager& disk_mgr);
    ~RecoveryManager() = default;

    DBErrorCode recover(BufferPoolManager& cache_manager);
};

} // namespace FenrirDB

#endif // FENRIRDB_WAL_H
