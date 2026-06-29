#include "wal.h"
#include "logger.h"
#include <cstring>
#include <sstream>

namespace FenrirDB {

std::vector<uint8_t> LogRecord::serialize() const {
    std::vector<uint8_t> bytes;

    // LSN (8 bytes)
    for (int i = 0; i < 8; ++i) {
        bytes.push_back((lsn >> (i * 8)) & 0xFF);
    }

    // TxID (4 bytes)
    for (int i = 0; i < 4; ++i) {
        bytes.push_back((tx_id >> (i * 8)) & 0xFF);
    }

    // Type (1 byte)
    bytes.push_back(static_cast<uint8_t>(type));

    // PageID (4 bytes)
    for (int i = 0; i < 4; ++i) {
        bytes.push_back((page_id >> (i * 8)) & 0xFF);
    }

    // SlotID (2 bytes)
    bytes.push_back(slot_id & 0xFF);
    bytes.push_back((slot_id >> 8) & 0xFF);

    // Before image length (2 bytes)
    uint16_t before_len = static_cast<uint16_t>(before_image.size());
    bytes.push_back(before_len & 0xFF);
    bytes.push_back((before_len >> 8) & 0xFF);
    bytes.insert(bytes.end(), before_image.begin(), before_image.end());

    // After image length (2 bytes)
    uint16_t after_len = static_cast<uint16_t>(after_image.size());
    bytes.push_back(after_len & 0xFF);
    bytes.push_back((after_len >> 8) & 0xFF);
    bytes.insert(bytes.end(), after_image.begin(), after_image.end());

    return bytes;
}

LogRecord LogRecord::deserialize(const std::vector<uint8_t>& bytes, size_t& offset) {
    LogRecord rec;
    if (offset + 21 > bytes.size()) {
        offset = bytes.size(); // EOF or corrupt
        return rec;
    }

    // LSN
    rec.lsn = 0;
    for (int i = 0; i < 8; ++i) {
        rec.lsn |= (static_cast<uint64_t>(bytes[offset++]) << (i * 8));
    }

    // TxID
    rec.tx_id = 0;
    for (int i = 0; i < 4; ++i) {
        rec.tx_id |= (static_cast<uint32_t>(bytes[offset++]) << (i * 8));
    }

    // Type
    rec.type = static_cast<LogRecordType>(bytes[offset++]);

    // PageID
    rec.page_id = 0;
    for (int i = 0; i < 4; ++i) {
        rec.page_id |= (static_cast<uint32_t>(bytes[offset++]) << (i * 8));
    }

    // SlotID
    rec.slot_id = bytes[offset] | (bytes[offset + 1] << 8);
    offset += 2;

    // Before image
    if (offset + 2 > bytes.size()) {
        offset = bytes.size();
        return rec;
    }
    uint16_t before_len = bytes[offset] | (bytes[offset + 1] << 8);
    offset += 2;

    if (offset + before_len > bytes.size()) {
        offset = bytes.size();
        return rec;
    }
    if (before_len > 0) {
        rec.before_image.assign(bytes.begin() + offset, bytes.begin() + offset + before_len);
        offset += before_len;
    }

    // After image
    if (offset + 2 > bytes.size()) {
        offset = bytes.size();
        return rec;
    }
    uint16_t after_len = bytes[offset] | (bytes[offset + 1] << 8);
    offset += 2;

    if (offset + after_len > bytes.size()) {
        offset = bytes.size();
        return rec;
    }
    if (after_len > 0) {
        rec.after_image.assign(bytes.begin() + offset, bytes.begin() + offset + after_len);
        offset += after_len;
    }

    return rec;
}

// ======================================================================
// LogManager Implementation
// ======================================================================

LogManager::LogManager(const std::string& filename) : log_filename(filename) {
    log_file.open(log_filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!log_file.is_open()) {
        log_file.open(log_filename, std::ios::out | std::ios::binary | std::ios::trunc);
        log_file.close();
        log_file.open(log_filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }
    Logger::get_instance().info("LogManager", "Initialized WAL file: " + log_filename);
}

LogManager::~LogManager() {
    flush();
    if (log_file.is_open()) {
        log_file.close();
    }
}

uint64_t LogManager::append_record(uint32_t tx_id, LogRecordType type, uint32_t page_id, uint16_t slot_id,
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
    if (log_file.is_open()) {
        log_file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    return rec.lsn;
}

void LogManager::flush() {
    if (log_file.is_open()) {
        log_file.flush();
        flushed_lsn = next_lsn - 1;
        Logger::get_instance().info("LogManager", "Flushed WAL up to LSN: " + std::to_string(flushed_lsn));
    }
}

std::vector<LogRecord> LogManager::read_all_records() {
    std::vector<LogRecord> records;
    std::ifstream in(log_filename, std::ios::binary);
    if (!in.is_open()) return records;

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t offset = 0;
    while (offset < buffer.size()) {
        LogRecord rec = LogRecord::deserialize(buffer, offset);
        if (offset <= buffer.size() && rec.lsn != 0) {
            records.push_back(rec);
            next_lsn = std::max(next_lsn, rec.lsn + 1);
        } else {
            break;
        }
    }
    return records;
}

// ======================================================================
// RecoveryManager Implementation
// ======================================================================

RecoveryManager::RecoveryManager(LogManager& log_mgr, DiskManager& disk_mgr)
    : log_manager(log_mgr), disk_manager(disk_mgr) {}

DBErrorCode RecoveryManager::recover(BufferPoolManager& cache_manager) {
    Logger::get_instance().info("Recovery", "Starting database WAL recovery pass...");
    
    std::vector<LogRecord> log_records = log_manager.read_all_records();
    std::vector<uint32_t> active_txs; // Keep track of uncommitted transactions
    
    // Injected Bug 8: Caching raw page pointers without invalidation on eviction
    std::unordered_map<uint32_t, Page*> recovery_page_cache;

    // Phase 1: Analysis & Redo Pass
    for (const auto& rec : log_records) {
        if (rec.type == LogRecordType::BEGIN) {
            active_txs.push_back(rec.tx_id);
        } else if (rec.type == LogRecordType::COMMIT || rec.type == LogRecordType::ABORT) {
            active_txs.erase(std::remove(active_txs.begin(), active_txs.end(), rec.tx_id), active_txs.end());
        } else if (rec.type == LogRecordType::INSERT || rec.type == LogRecordType::UPDATE || rec.type == LogRecordType::DELETE) {
            Page* page = nullptr;
            auto cache_it = recovery_page_cache.find(rec.page_id);
            if (cache_it != recovery_page_cache.end()) {
                page = cache_it->second;
            } else {
                page = cache_manager.fetch_page(rec.page_id);
                recovery_page_cache[rec.page_id] = page;
            }

            if (page) {
                if (rec.type == LogRecordType::INSERT || rec.type == LogRecordType::UPDATE) {
                    page->update_record(rec.slot_id, rec.after_image.data(), static_cast<uint16_t>(rec.after_image.size()));
                } else if (rec.type == LogRecordType::DELETE) {
                    page->delete_record(rec.slot_id);
                }
                cache_manager.flush_page(rec.page_id);
            }
        }
    }
    Logger::get_instance().info("Recovery", "Redo pass complete. Active transactions: " + std::to_string(active_txs.size()));

    // Phase 2: Undo Pass
    for (auto it = log_records.rbegin(); it != log_records.rend(); ++it) {
        const auto& rec = *it;
        if (std::find(active_txs.begin(), active_txs.end(), rec.tx_id) != active_txs.end()) {
            if (rec.type == LogRecordType::INSERT || rec.type == LogRecordType::UPDATE) {
                Page* page = nullptr;
                auto cache_it = recovery_page_cache.find(rec.page_id);
                if (cache_it != recovery_page_cache.end()) {
                    page = cache_it->second;
                } else {
                    page = cache_manager.fetch_page(rec.page_id);
                    recovery_page_cache[rec.page_id] = page;
                }

                if (page) {
                    if (rec.before_image.empty()) {
                        page->delete_record(rec.slot_id);
                    } else {
                        page->update_record(rec.slot_id, rec.before_image.data(), static_cast<uint16_t>(rec.before_image.size()));
                    }
                    cache_manager.flush_page(rec.page_id);
                }
            } else if (rec.type == LogRecordType::DELETE) {
                Page* page = nullptr;
                auto cache_it = recovery_page_cache.find(rec.page_id);
                if (cache_it != recovery_page_cache.end()) {
                    page = cache_it->second;
                } else {
                    page = cache_manager.fetch_page(rec.page_id);
                    recovery_page_cache[rec.page_id] = page;
                }

                if (page) {
                    page->update_record(rec.slot_id, rec.before_image.data(), static_cast<uint16_t>(rec.before_image.size()));
                    cache_manager.flush_page(rec.page_id);
                }
            }
        }
    }

    cache_manager.flush_all();
    Logger::get_instance().info("Recovery", "Database recovery complete. All transactions rolled back / committed cleanly.");
    return DBErrorCode::SUCCESS;
}

} // namespace FenrirDB
