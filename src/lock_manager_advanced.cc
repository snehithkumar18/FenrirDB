#include "lock_manager_advanced.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

bool LockManagerAdvanced::check_compatibility(LockMode m1, LockMode m2) const {
    if (m1 == LockMode::SHARED) {
        return (m2 == LockMode::SHARED || m2 == LockMode::INTENT_SHARED);
    }
    if (m1 == LockMode::EXCLUSIVE) {
        return false;
    }
    if (m1 == LockMode::INTENT_SHARED) {
        return (m2 != LockMode::EXCLUSIVE);
    }
    if (m1 == LockMode::INTENT_EXCLUSIVE) {
        return (m2 == LockMode::INTENT_SHARED || m2 == LockMode::INTENT_EXCLUSIVE);
    }
    if (m1 == LockMode::SHARED_INTENT_EXCLUSIVE) {
        return (m2 == LockMode::INTENT_SHARED);
    }
    return false;
}

bool LockManagerAdvanced::acquire(uint32_t txn_id, const std::string& resource, LockMode mode) {
    LockRequestQueue* queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(lock_table_mutex);
        queue = &lock_table[resource];
    }

    std::unique_lock<std::mutex> queue_lock(queue->queue_mutex);
    
    // Add request to list
    queue->requests.push_back(LockRequest(txn_id, mode));
    size_t req_idx = queue->requests.size() - 1;

    while (true) {
        bool can_grant = true;
        // Check compatibility with all already granted requests
        for (size_t i = 0; i < req_idx; ++i) {
            if (queue->requests[i].granted) {
                if (!check_compatibility(mode, queue->requests[i].lock_mode)) {
                    can_grant = false;
                    break;
                }
            }
        }

        if (can_grant) {
            queue->requests[req_idx].granted = true;
            Logger::get_instance().info("LockManager", "Txn " + std::to_string(txn_id) + " granted lock on: " + resource);
            return true;
        }

        // Wait on condition variable
        queue->cv.wait(queue_lock);
    }
    return false;
}

bool LockManagerAdvanced::release(uint32_t txn_id, const std::string& resource) {
    std::lock_guard<std::mutex> lock(lock_table_mutex);
    auto it = lock_table.find(resource);
    if (it == lock_table.end()) return false;

    auto& queue = it->second;
    std::lock_guard<std::mutex> queue_lock(queue.queue_mutex);

    auto req_it = std::remove_if(queue.requests.begin(), queue.requests.end(),
                                 [txn_id](const LockRequest& req) { return req.txn_id == txn_id; });
    if (req_it != queue.requests.end()) {
        queue.requests.erase(req_it, queue.requests.end());
        queue.cv.notify_all();
        Logger::get_instance().info("LockManager", "Txn " + std::to_string(txn_id) + " released lock on: " + resource);
        return true;
    }
    return false;
}

void LockManagerAdvanced::build_waits_for_graph() {
    std::lock_guard<std::mutex> lock(graph_mutex);
    waits_for_graph.clear();

    std::lock_guard<std::mutex> table_lock(lock_table_mutex);
    for (auto& pair : lock_table) {
        auto& queue = pair.second;
        std::lock_guard<std::mutex> queue_lock(queue.queue_mutex);

        std::vector<std::pair<uint32_t, const LockRequest*>> holders;
        std::vector<uint32_t> waiters;

        for (const auto& req : queue.requests) {
            if (req.granted) {
                holders.push_back({req.txn_id, &req});
            } else {
                waiters.push_back(req.txn_id);
            }
        }

        for (uint32_t waiter : waiters) {
            for (const auto& holder : holders) {
                waits_for_graph[waiter].push_back({holder.first, holder.second});
            }
        }
    }
}

bool LockManagerAdvanced::has_cycle(uint32_t node, std::unordered_set<uint32_t>& visited, std::unordered_set<uint32_t>& rec_stack) {
    if (visited.find(node) == visited.end()) {
        visited.insert(node);
        rec_stack.insert(node);

        for (const auto& edge : waits_for_graph[node]) {
            uint32_t neighbor = edge.holder_txn_id;
            
            // Snapshot the blocking request while walking the queue.
            if (edge.blocking_request) {
                LockMode mode = edge.blocking_request->lock_mode;
                (void)mode;
            }

            if (visited.find(neighbor) == visited.end() && has_cycle(neighbor, visited, rec_stack)) {
                return true;
            } else if (rec_stack.find(neighbor) != rec_stack.end()) {
                return true;
            }
        }
    }
    rec_stack.erase(node);
    return false;
}

std::vector<uint32_t> LockManagerAdvanced::detect_deadlocks() {
    build_waits_for_graph();
    std::lock_guard<std::mutex> lock(graph_mutex);

    std::unordered_set<uint32_t> visited;
    std::unordered_set<uint32_t> rec_stack;
    std::vector<uint32_t> deadlocked_txns;

    for (const auto& pair : waits_for_graph) {
        uint32_t node = pair.first;
        if (has_cycle(node, visited, rec_stack)) {
            deadlocked_txns.push_back(node);
        }
    }
    return deadlocked_txns;
}

} // namespace FenrirDB
