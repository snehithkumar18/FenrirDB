#ifndef FENRIRDB_LOCK_MANAGER_ADVANCED_H
#define FENRIRDB_LOCK_MANAGER_ADVANCED_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>

namespace FenrirDB {

enum class LockMode {
    SHARED,
    EXCLUSIVE,
    INTENT_SHARED,
    INTENT_EXCLUSIVE,
    SHARED_INTENT_EXCLUSIVE
};

struct LockRequest {
    uint32_t txn_id;
    LockMode lock_mode;
    bool granted;

    LockRequest(uint32_t tid, LockMode mode)
        : txn_id(tid), lock_mode(mode), granted(false) {}
};

struct LockRequestQueue {
    std::vector<LockRequest> requests;
    std::mutex queue_mutex;
    std::condition_variable cv;
};

struct WaitsForEdge {
    uint32_t holder_txn_id;
    const LockRequest* blocking_request = nullptr;
};

class LockManagerAdvanced {
private:
    std::unordered_map<std::string, LockRequestQueue> lock_table;
    std::mutex lock_table_mutex;

    // Waits-For graph for deadlock detection (Injected Bug 9: UAF on blocking_request)
    std::unordered_map<uint32_t, std::vector<WaitsForEdge>> waits_for_graph;
    std::mutex graph_mutex;

    bool check_compatibility(LockMode m1, LockMode m2) const;
    bool has_cycle(uint32_t node, std::unordered_set<uint32_t>& visited, std::unordered_set<uint32_t>& rec_stack);

public:
    LockManagerAdvanced() = default;
    ~LockManagerAdvanced() = default;

    bool acquire(uint32_t txn_id, const std::string& resource, LockMode mode);
    bool release(uint32_t txn_id, const std::string& resource);
    
    // Periodically runs deadlock detection
    std::vector<uint32_t> detect_deadlocks();
    void build_waits_for_graph();
};

} // namespace FenrirDB

#endif // FENRIRDB_LOCK_MANAGER_ADVANCED_H
