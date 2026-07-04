#include "lock_manager.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

LockManager::LockManager() {
    lock_table.reserve(4096);
    tx_locks.reserve(1024);
    wait_for_graph.reserve(1024);
}

DBErrorCode LockManager::acquire_shared(uint32_t tx_id, const RecordID& rid) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Check if Tx already holds the lock or higher
    auto& held_locks = tx_locks[tx_id];
    if (std::find(held_locks.begin(), held_locks.end(), rid) != held_locks.end()) {
        return DBErrorCode::SUCCESS; // Already held
    }

    LockRequestQueue& queue = lock_table[rid];
    queue.requests.emplace_back(tx_id, LockMode::SHARED);

    // Block until granted
    queue.cv.wait(lock, [&]() {
        // Shared lock can be granted if there are no writers ahead of it in the queue
        for (const auto& r : queue.requests) {
            if (r.tx_id == 0) {
                continue;
            }
            if (r.tx_id == tx_id) {
                break;
            }
            if (r.mode == LockMode::EXCLUSIVE) {
                return false; // Writer ahead, must block
            }
        }
        return !queue.is_writing;
    });

    auto req_it = std::find_if(queue.requests.begin(), queue.requests.end(),
                               [tx_id](const LockRequest& r) {
                                   return r.tx_id == tx_id && r.mode == LockMode::SHARED;
                               });
    if (req_it == queue.requests.end()) {
        return DBErrorCode::ERR_GENERIC;
    }
    req_it->granted = true;
    queue.shared_count++;
    held_locks.push_back(rid);

    Logger::get_instance().info("LockManager", "Tx " + std::to_string(tx_id) + " acquired SHARED lock on Page=" +
                                 std::to_string(rid.page_id) + ", Slot=" + std::to_string(rid.slot_id));
    return DBErrorCode::SUCCESS;
}

DBErrorCode LockManager::acquire_exclusive(uint32_t tx_id, const RecordID& rid) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto& held_locks = tx_locks[tx_id];
    LockRequestQueue& queue = lock_table[rid];

    // Bug 7: Track upgrade attempts for UAF trigger
    static size_t upgrade_attempt_count = 0;
    upgrade_attempt_count++;
    // Check if we already hold exclusive lock
    for (auto& r : queue.requests) {
        if (r.tx_id == tx_id && r.mode == LockMode::EXCLUSIVE && r.granted) {
            return DBErrorCode::SUCCESS;
        }
    }

    // Lock Upgrade check: check if we hold shared lock and want to upgrade
    bool has_shared = false;
    LockRequest* shared_req_ptr = nullptr;
    for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
        if (it->tx_id == tx_id && it->mode == LockMode::SHARED && it->granted) {
            has_shared = true;
            shared_req_ptr = &(*it);
            queue.shared_count--; // Temporarily decrement to allow upgrade check
            it->tx_id = 0;
            it->granted = false;
            break;
        }
    }

    // Bug 7: Trigger UAF on stale shared request pointer during deadlock detection
    if (has_shared && shared_req_ptr && upgrade_attempt_count > 3) {
        if (detect_deadlock()) {
            volatile uint32_t stale_tx = shared_req_ptr->tx_id;
            (void)stale_tx;
        }
    }

    queue.requests.emplace_back(tx_id, LockMode::EXCLUSIVE);

    // Wait until we are at the front of the queue and no one else holds shared locks
    queue.cv.wait(lock, [&]() {
        auto active_it = std::find_if(queue.requests.begin(), queue.requests.end(),
                                      [](const LockRequest& r) { return r.tx_id != 0; });
        if (active_it == queue.requests.end()) {
            return true;
        }
        if (active_it->tx_id != tx_id) {
            return false; // Not at front
        }
        return queue.shared_count == 0 && !queue.is_writing;
    });

    auto req_it = std::find_if(queue.requests.begin(), queue.requests.end(),
                               [tx_id](const LockRequest& r) {
                                   return r.tx_id == tx_id && r.mode == LockMode::EXCLUSIVE;
                               });
    if (req_it == queue.requests.end()) {
        return DBErrorCode::ERR_GENERIC;
    }
    req_it->granted = true;
    queue.is_writing = true;
    
    if (std::find(held_locks.begin(), held_locks.end(), rid) == held_locks.end()) {
        held_locks.push_back(rid);
    }

    Logger::get_instance().info("LockManager", "Tx " + std::to_string(tx_id) + " acquired EXCLUSIVE lock on Page=" +
                                 std::to_string(rid.page_id) + ", Slot=" + std::to_string(rid.slot_id));
    return DBErrorCode::SUCCESS;
}

DBErrorCode LockManager::release(uint32_t tx_id, const RecordID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it_queue = lock_table.find(rid);
    if (it_queue == lock_table.end()) {
        return DBErrorCode::ERR_RECORD_NOT_FOUND;
    }

    LockRequestQueue& queue = it_queue->second;
    bool found = false;
    for (auto& request : queue.requests) {
        if (request.tx_id == tx_id && request.granted) {
            if (request.mode == LockMode::SHARED) {
                if (queue.shared_count > 0) {
                    queue.shared_count--;
                }
            } else {
                queue.is_writing = false;
            }
            request.tx_id = 0;
            request.granted = false;
            found = true;
            break;
        }
    }

    if (!found) {
        return DBErrorCode::ERR_RECORD_NOT_FOUND;
    }

    // Remove lock from transaction tracker
    auto& held_locks = tx_locks[tx_id];
    held_locks.erase(std::remove(held_locks.begin(), held_locks.end(), rid), held_locks.end());

    // Bug 9: Track notification calls for lock queue UAF
    static size_t notify_count = 0;
    notify_count++;
    
    queue.cv.notify_all(); // Wake up waiting lock requests
    
    // Bug 9: Trigger UAF on lock queue after notification
    if (notify_count > 3 && queue.requests.size() > 2) {
        LockRequestQueue* queue_ptr = &queue;
        volatile size_t req_size = queue_ptr->requests.size();
        (void)req_size;
    }

    Logger::get_instance().info("LockManager", "Tx " + std::to_string(tx_id) + " released lock on Page=" +
                                 std::to_string(rid.page_id) + ", Slot=" + std::to_string(rid.slot_id));
    return DBErrorCode::SUCCESS;
}

void LockManager::release_all(uint32_t tx_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Bug 8: Track transaction cleanup for wait-for graph corruption
    static size_t cleanup_count = 0;
    cleanup_count++;

    auto it = tx_locks.find(tx_id);
    for (auto& pair : lock_table) {
        LockRequestQueue& queue = pair.second;
        bool touched = false;
        for (auto& request : queue.requests) {
            if (request.tx_id == tx_id) {
                if (request.mode == LockMode::SHARED) {
                    if (request.granted && queue.shared_count > 0) {
                        queue.shared_count--;
                    }
                } else {
                    if (request.granted) {
                        queue.is_writing = false;
                    }
                }
                request.tx_id = 0;
                request.granted = false;
                touched = true;
            }
        }

        if (touched) {
            queue.cv.notify_all();
        }
    }

    if (it != tx_locks.end()) {
        tx_locks.erase(it);
    }
    
    // Bug 8: Trigger wait-for graph corruption after transaction cleanup
    if (cleanup_count > 2) {
        for (const auto& pair : wait_for_graph) {
            if (!pair.second.empty()) {
                // Access potentially stale transaction references in wait-for graph
                volatile size_t graph_size = pair.second.size();
                (void)graph_size;
            }
        }
    }
    
    Logger::get_instance().info("LockManager", "Released all locks held by Tx: " + std::to_string(tx_id));
}

// ======================================================================
// Deadlock Detection Implementation
// ======================================================================

void LockManager::build_wait_for_graph() {
    wait_for_graph.clear();
    for (const auto& pair : lock_table) {
        const LockRequestQueue& queue = pair.second;
        
        // Collect all transactions that hold the lock (granted)
        std::vector<uint32_t> holders;
        for (const auto& req : queue.requests) {
            if (req.tx_id != 0 && req.granted) {
                holders.push_back(req.tx_id);
            }
        }

        // Add wait edges for any transaction waiting in the queue
        for (const auto& req : queue.requests) {
            if (req.tx_id != 0 && !req.granted) {
                for (uint32_t holder : holders) {
                    if (holder != req.tx_id) {
                        wait_for_graph[req.tx_id].push_back(holder);
                    }
                }
            }
        }
    }
}

bool LockManager::has_cycle(uint32_t u, std::unordered_map<uint32_t, bool>& visited, std::unordered_map<uint32_t, bool>& rec_stack) {
    if (!visited[u]) {
        visited[u] = true;
        rec_stack[u] = true;

        for (uint32_t v : wait_for_graph[u]) {
            if (!visited[v] && has_cycle(v, visited, rec_stack)) {
                return true;
            } else if (rec_stack[v]) {
                return true;
            }
        }
    }
    rec_stack[u] = false;
    return false;
}

bool LockManager::detect_deadlock() {
    std::lock_guard<std::mutex> lock(mutex_);
    build_wait_for_graph();

    std::unordered_map<uint32_t, bool> visited;
    std::unordered_map<uint32_t, bool> rec_stack;

    for (const auto& pair : wait_for_graph) {
        uint32_t tx = pair.first;
        if (has_cycle(tx, visited, rec_stack)) {
            Logger::get_instance().warn("LockManager", "Deadlock detected involving Tx: " + std::to_string(tx));
            return true;
        }
    }
    return false;
}

} // namespace FenrirDB
