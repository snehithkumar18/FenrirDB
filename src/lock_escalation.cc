#include "lock_escalation.h"
#include "logger.h"

namespace FenrirDB {

LockEscalationManager::LockEscalationManager(LockManagerAdvanced* lm, size_t threshold)
    : lock_manager(lm), escalation_threshold(threshold) {}

void LockEscalationManager::track_lock(uint32_t txn_id, const std::string& resource) {
    std::lock_guard<std::mutex> lock(escalation_mutex);
    txn_locks[txn_id].insert(resource);
}

void LockEscalationManager::track_unlock(uint32_t txn_id, const std::string& resource) {
    std::lock_guard<std::mutex> lock(escalation_mutex);
    auto it = txn_locks.find(txn_id);
    if (it != txn_locks.end()) {
        it->second.erase(resource);
    }
}

bool LockEscalationManager::evaluate_and_escalate(uint32_t txn_id, const std::string& table_name) {
    std::lock_guard<std::mutex> lock(escalation_mutex);
    
    auto it = txn_locks.find(txn_id);
    if (it == txn_locks.end()) return false;

    // Filter locks belonging to the target table (e.g., "users_row_123")
    std::vector<std::string> row_locks;
    std::string prefix = table_name + "_row_";
    for (const auto& res : it->second) {
        if (res.rfind(prefix, 0) == 0) {
            row_locks.push_back(res);
        }
    }

    if (row_locks.size() >= escalation_threshold && lock_manager) {
        Logger::get_instance().info("Escalation", "Txn " + std::to_string(txn_id) + " holds " + std::to_string(row_locks.size()) + " locks on " + table_name + ". Escalation threshold reached.");

        // Release row locks
        for (const auto& row_res : row_locks) {
            lock_manager->release(txn_id, row_res);
            it->second.erase(row_res);
        }

        // Acquire single table lock
        lock_manager->acquire(txn_id, table_name, LockMode::EXCLUSIVE);
        it->second.insert(table_name);

        Logger::get_instance().info("Escalation", "Escalated row locks to table lock on: " + table_name);
        return true;
    }

    return false;
}

} // namespace FenrirDB
