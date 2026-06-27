#ifndef FENRIRDB_LOCK_MANAGER_H
#define FENRIRDB_LOCK_MANAGER_H

#include <unordered_map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include "storage.h"
#include "errors.h"
#include "index.h"

namespace FenrirDB {

enum class LockMode : uint8_t {
    SHARED = 0,
    EXCLUSIVE = 1
};

struct LockRequest {
    uint32_t tx_id;
    LockMode mode;
    bool granted = false;

    LockRequest(uint32_t tx, LockMode m) : tx_id(tx), mode(m) {}
};

struct LockRequestQueue {
    std::vector<LockRequest> requests;
    std::condition_variable cv;
    bool is_writing = false;
    int shared_count = 0;
};

class LockManager {
private:
    std::mutex mutex_;
    
    // Hash function for RecordID to use in unordered_map
    struct RecordIDHash {
        size_t operator()(const RecordID& rid) const {
            return (static_cast<size_t>(rid.page_id) << 16) ^ rid.slot_id;
        }
    };

    std::unordered_map<RecordID, LockRequestQueue, RecordIDHash> lock_table;
    std::unordered_map<uint32_t, std::vector<RecordID>> tx_locks; // Track locks held by each Tx

    // Deadlock detection wait-for-graph
    std::unordered_map<uint32_t, std::vector<uint32_t>> wait_for_graph;
    
    bool has_cycle(uint32_t start_tx, std::unordered_map<uint32_t, bool>& visited, std::unordered_map<uint32_t, bool>& rec_stack);
    void build_wait_for_graph();

public:
    LockManager() = default;
    ~LockManager() = default;

    DBErrorCode acquire_shared(uint32_t tx_id, const RecordID& rid);
    DBErrorCode acquire_exclusive(uint32_t tx_id, const RecordID& rid);
    DBErrorCode release(uint32_t tx_id, const RecordID& rid);
    void release_all(uint32_t tx_id);

    bool detect_deadlock(); // Returns true if a deadlock (cycle) was detected and aborted
};

} // namespace FenrirDB

#endif // FENRIRDB_LOCK_MANAGER_H
