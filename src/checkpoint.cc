#include "checkpoint.h"
#include "logger.h"
#include <cstring>
#include <algorithm>

namespace FenrirDB {

std::vector<uint8_t> CheckpointData::serialize() const {
    std::vector<uint8_t> bytes;

    // 1. Serialize dirty page table
    uint32_t num_dirty = static_cast<uint32_t>(dirty_page_table.size());
    bytes.push_back(num_dirty & 0xFF);
    bytes.push_back((num_dirty >> 8) & 0xFF);
    bytes.push_back((num_dirty >> 16) & 0xFF);
    bytes.push_back((num_dirty >> 24) & 0xFF);

    for (const auto& pair : dirty_page_table) {
        uint32_t pid = pair.first;
        uint64_t lsn = pair.second;
        
        // PageID
        for (int i = 0; i < 4; ++i) bytes.push_back((pid >> (i * 8)) & 0xFF);
        // LSN
        for (int i = 0; i < 8; ++i) bytes.push_back((lsn >> (i * 8)) & 0xFF);
    }

    // 2. Serialize active transaction table
    uint32_t num_txs = static_cast<uint32_t>(active_tx_table.size());
    bytes.push_back(num_txs & 0xFF);
    bytes.push_back((num_txs >> 8) & 0xFF);
    bytes.push_back((num_txs >> 16) & 0xFF);
    bytes.push_back((num_txs >> 24) & 0xFF);

    for (const auto& pair : active_tx_table) {
        uint32_t tid = pair.first;
        uint64_t lsn = pair.second;

        // TxID
        for (int i = 0; i < 4; ++i) bytes.push_back((tid >> (i * 8)) & 0xFF);
        // LSN
        for (int i = 0; i < 8; ++i) bytes.push_back((lsn >> (i * 8)) & 0xFF);
    }

    return bytes;
}

CheckpointData CheckpointData::deserialize(const std::vector<uint8_t>& bytes) {
    CheckpointData data;
    if (bytes.size() < 8) return data;

    size_t offset = 0;
    
    // 1. Deserialize dirty pages
    uint32_t num_dirty = bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24);
    offset += 4;

    for (uint32_t i = 0; i < num_dirty; ++i) {
        if (offset + 12 > bytes.size()) break;
        uint32_t pid = 0;
        for (int j = 0; j < 4; ++j) pid |= (static_cast<uint32_t>(bytes[offset++]) << (j * 8));

        uint64_t lsn = 0;
        for (int j = 0; j < 8; ++j) lsn |= (static_cast<uint64_t>(bytes[offset++]) << (j * 8));

        data.dirty_page_table[pid] = lsn;
    }

    // 2. Deserialize active txs
    if (offset + 4 > bytes.size()) return data;
    uint32_t num_txs = bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24);
    offset += 4;

    for (uint32_t i = 0; i < num_txs; ++i) {
        if (offset + 12 > bytes.size()) break;
        uint32_t tid = 0;
        for (int j = 0; j < 4; ++j) tid |= (static_cast<uint32_t>(bytes[offset++]) << (j * 8));

        uint64_t lsn = 0;
        for (int j = 0; j < 8; ++j) lsn |= (static_cast<uint64_t>(bytes[offset++]) << (j * 8));

        data.active_tx_table[tid] = lsn;
    }

    return data;
}

// ======================================================================
// CheckpointManager Implementation
// ======================================================================

CheckpointManager::CheckpointManager(LogManager& lm, BufferPoolManager& cm, TransactionManager& tm)
    : log_manager(lm), cache_manager(cm), tx_manager(tm) {
    Logger::get_instance().info("Checkpoint", "CheckpointManager initialized.");
}

void CheckpointManager::mark_page_dirty(uint32_t page_id, uint64_t lsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dirty_pages.find(page_id) == dirty_pages.end()) {
        dirty_pages[page_id] = lsn; // oldest un-flushed LSN
    }
}

void CheckpointManager::mark_page_clean(uint32_t page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_pages.erase(page_id);
}

uint64_t CheckpointManager::begin_checkpoint() {
    std::lock_guard<std::mutex> lock(mutex_);
    Logger::get_instance().info("Checkpoint", "Executing database checkpoint...");

    CheckpointData data;
    data.dirty_page_table = dirty_pages;

    // Collect active transactions from TxManager
    for (uint32_t tx_id = 1; tx_id < 1000; ++tx_id) { // Scan transaction range
        auto tx = tx_manager.get_tx(tx_id);
        if (tx && tx->state == TxState::ACTIVE) {
            data.active_tx_table[tx_id] = tx->read_ts;
        }
    }

    // Write checkpoint records to WAL
    log_manager.append_record(0, LogRecordType::BEGIN);
    std::vector<uint8_t> bytes = data.serialize();
    uint64_t end_lsn = log_manager.append_record(0, LogRecordType::COMMIT, 0, 0, {}, bytes);
    log_manager.flush();

    // Determine the oldest LSN we still need for recovery
    uint64_t min_lsn = end_lsn;
    for (const auto& pair : dirty_pages) {
        min_lsn = std::min(min_lsn, pair.second);
    }
    for (const auto& pair : data.active_tx_table) {
        min_lsn = std::min(min_lsn, pair.second);
    }

    // Flush dirty pages to disk
    cache_manager.flush_all();
    dirty_pages.clear(); // Flushed and clean now

    Logger::get_instance().info("Checkpoint", "Checkpoint complete. Oldest un-reclaimed LSN boundary: " + std::to_string(min_lsn));
    return min_lsn;
}

} // namespace FenrirDB
