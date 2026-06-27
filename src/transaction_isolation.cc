#include "transaction_isolation.h"
#include "logger.h"

namespace FenrirDB {

void TransactionIsolationManager::set_isolation_level(uint32_t txn_id, IsolationLevel level) {
    txn_levels[txn_id] = level;
    std::string level_name = "READ_COMMITTED";
    if (level == IsolationLevel::READ_UNCOMMITTED) level_name = "READ_UNCOMMITTED";
    else if (level == IsolationLevel::REPEATABLE_READ) level_name = "REPEATABLE_READ";
    else if (level == IsolationLevel::SERIALIZABLE) level_name = "SERIALIZABLE";
    
    Logger::get_instance().info("Isolation", "Set txn: " + std::to_string(txn_id) + " to level: " + level_name);
}

IsolationLevel TransactionIsolationManager::get_isolation_level(uint32_t txn_id) const {
    auto it = txn_levels.find(txn_id);
    if (it == txn_levels.end()) {
        return IsolationLevel::READ_COMMITTED; // Default
    }
    return it->second;
}

bool TransactionIsolationManager::is_version_visible(uint32_t txn_id, uint64_t version_lsn, uint64_t current_read_ts) const {
    IsolationLevel level = get_isolation_level(txn_id);

    if (level == IsolationLevel::READ_UNCOMMITTED) {
        // Dirty reads allowed: all uncommitted changes are visible
        return true;
    }

    if (level == IsolationLevel::READ_COMMITTED) {
        // Only committed changes are visible: LSN must be older than current active read timestamp bounds
        return version_lsn <= current_read_ts;
    }

    if (level == IsolationLevel::REPEATABLE_READ || level == IsolationLevel::SERIALIZABLE) {
        // Must use strict snapshot timestamp
        return version_lsn <= current_read_ts;
    }

    return false;
}

} // namespace FenrirDB
