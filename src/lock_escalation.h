#ifndef FENRIRDB_LOCK_ESCALATION_H
#define FENRIRDB_LOCK_ESCALATION_H

#include "lock_manager_advanced.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace FenrirDB {

class LockEscalationManager {
private:
    LockManagerAdvanced* lock_manager;
    size_t escalation_threshold;
    
    // Map: txn_id -> set of resources locked by it
    std::unordered_map<uint32_t, std::unordered_set<std::string>> txn_locks;
    std::mutex escalation_mutex;

public:
    LockEscalationManager(LockManagerAdvanced* lm, size_t threshold = 100);
    ~LockEscalationManager() = default;

    void track_lock(uint32_t txn_id, const std::string& resource);
    void track_unlock(uint32_t txn_id, const std::string& resource);
    
    // Evaluates if lock escalation is required and executes it
    bool evaluate_and_escalate(uint32_t txn_id, const std::string& table_name);
};

} // namespace FenrirDB

#endif // FENRIRDB_LOCK_ESCALATION_H
