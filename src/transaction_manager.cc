#include "transaction_manager.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

TransactionManager::TransactionManager(LogManager& lm, LockManager& lkm)
    : log_manager(lm), lock_manager(lkm) {
    Logger::get_instance().info("TxManager", "TransactionManager initialized.");
}

std::shared_ptr<Transaction> TransactionManager::begin_tx() {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t tx_id = next_tx_id++;
    uint64_t read_ts = log_manager.get_next_lsn() - 1; // Current system version LSN
    
    auto tx = std::make_shared<Transaction>(tx_id, read_ts);
    tx_table[tx_id] = tx;

    log_manager.append_record(tx_id, LogRecordType::BEGIN);
    Logger::get_instance().info("TxManager", "Transaction " + std::to_string(tx_id) + " started with ReadTS=" + std::to_string(read_ts));
    return tx;
}

DBErrorCode TransactionManager::commit_tx(uint32_t tx_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tx_table.find(tx_id);
    if (it == tx_table.end()) {
        return DBErrorCode::ERR_GENERIC;
    }

    std::shared_ptr<Transaction> tx = it->second;
    tx->state = TxState::COMMITTED;
    tx->commit_ts = log_manager.get_next_lsn();

    log_manager.append_record(tx_id, LogRecordType::COMMIT);
    log_manager.flush();

    // Release all locks held by transaction
    lock_manager.release_all(tx_id);
    
    Logger::get_instance().info("TxManager", "Transaction " + std::to_string(tx_id) + " committed at CommitTS=" + std::to_string(tx->commit_ts));
    return DBErrorCode::SUCCESS;
}

DBErrorCode TransactionManager::abort_tx(uint32_t tx_id, BufferPoolManager& cache_mgr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tx_table.find(tx_id);
    if (it == tx_table.end()) {
        return DBErrorCode::ERR_GENERIC;
    }

    std::shared_ptr<Transaction> tx = it->second;
    tx->state = TxState::ABORTED;

    Logger::get_instance().info("TxManager", "Aborting Transaction " + std::to_string(tx_id) + ". Sweping WAL for rollbacks...");

    // Rollback changes by reading WAL in reverse order
    std::vector<LogRecord> records = log_manager.read_all_records();
    for (auto rit = records.rbegin(); rit != records.rend(); ++rit) {
        const LogRecord& rec = *rit;
        if (rec.tx_id == tx_id) {
            if (rec.type == LogRecordType::INSERT || rec.type == LogRecordType::UPDATE) {
                Page* page = cache_mgr.fetch_page(rec.page_id);
                if (page) {
                    if (rec.before_image.empty()) {
                        page->delete_record(rec.slot_id);
                    } else {
                        page->update_record(rec.slot_id, rec.before_image.data(), static_cast<uint16_t>(rec.before_image.size()));
                    }
                    cache_mgr.flush_page(rec.page_id);
                }
            } else if (rec.type == LogRecordType::DELETE) {
                Page* page = cache_mgr.fetch_page(rec.page_id);
                if (page) {
                    page->update_record(rec.slot_id, rec.before_image.data(), static_cast<uint16_t>(rec.before_image.size()));
                    cache_mgr.flush_page(rec.page_id);
                }
            }
        }
    }

    log_manager.append_record(tx_id, LogRecordType::ABORT);
    log_manager.flush();

    // Release locks
    lock_manager.release_all(tx_id);

    Logger::get_instance().info("TxManager", "Transaction " + std::to_string(tx_id) + " aborted and rolled back successfully.");
    return DBErrorCode::SUCCESS;
}

bool TransactionManager::is_visible(uint32_t tx_id, uint64_t record_lsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tx_table.find(tx_id);
    if (it == tx_table.end()) {
        return true; // Standalone autocommit query sees everything
    }

    std::shared_ptr<Transaction> tx = it->second;
    
    // Visibility rules:
    // 1. A record is visible if record LSN is less than or equal to the transaction read timestamp
    return record_lsn <= tx->read_ts;
}

std::shared_ptr<Transaction> TransactionManager::get_tx(uint32_t tx_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tx_table.find(tx_id);
    if (it != tx_table.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace FenrirDB
