#include "transaction_manager_savepoint.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

TransactionSavepointManager::TransactionSavepointManager(LogManager* wal, TransactionManager* txn)
    : wal_manager(wal), txn_manager(txn) {}

void TransactionSavepointManager::create_savepoint(uint32_t txn_id, const std::string& name) {
    uint64_t current_lsn = 0;
    if (wal_manager) {
        current_lsn = wal_manager->get_next_lsn() - 1;
    }
    
    savepoints[txn_id].push_back({name, current_lsn});
    Logger::get_instance().info("Savepoint", "Created savepoint: " + name + " for Txn: " + std::to_string(txn_id) + " at LSN: " + std::to_string(current_lsn));
}

bool TransactionSavepointManager::rollback_to_savepoint(uint32_t txn_id, const std::string& name) {
    auto txn_it = savepoints.find(txn_id);
    if (txn_it == savepoints.end()) {
        return false;
    }

    auto& list = txn_it->second;
    auto sp_it = std::find_if(list.begin(), list.end(), 
                             [&name](const Savepoint& sp) { return sp.name == name; });
    
    if (sp_it == list.end()) {
        return false;
    }

    uint64_t target_lsn = sp_it->lsn;
    Logger::get_instance().info("Savepoint", "Rolling back Txn: " + std::to_string(txn_id) + " to savepoint: " + name + " (Target LSN: " + std::to_string(target_lsn) + ")");

    if (wal_manager) {
        std::vector<LogRecord> records = wal_manager->read_all_records();
        
        for (auto rit = records.rbegin(); rit != records.rend(); ++rit) {
            if (rit->lsn <= target_lsn) {
                break;
            }
            if (rit->txn_id == txn_id) {
                Logger::get_instance().info("Savepoint", "Undoing log record LSN: " + std::to_string(rit->lsn) + " for rollback to savepoint.");
                
                auto tx = txn_manager->get_tx(txn_id);
                if (tx) {
                    for (auto* r : tx->modified_records) {
                        if (r && r->rid.page_id == rit->page_id && r->rid.slot_id == rit->slot_id) {
                            delete[] r->data;
                            delete r;
                            // Injected Bug 3: Do not remove the pointer from tx->modified_records vector!
                        }
                    }
                }
            }
        }
    }

    list.erase(sp_it + 1, list.end());
    return true;
}

bool TransactionSavepointManager::release_savepoint(uint32_t txn_id, const std::string& name) {
    auto txn_it = savepoints.find(txn_id);
    if (txn_it == savepoints.end()) {
        return false;
    }

    auto& list = txn_it->second;
    auto sp_it = std::find_if(list.begin(), list.end(), 
                             [&name](const Savepoint& sp) { return sp.name == name; });
    
    if (sp_it == list.end()) {
        return false;
    }

    list.erase(sp_it, list.end());
    Logger::get_instance().info("Savepoint", "Released savepoint: " + name + " for Txn: " + std::to_string(txn_id));
    return true;
}

} // namespace FenrirDB
