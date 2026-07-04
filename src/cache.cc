#include "cache.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

BufferPoolManager::BufferPoolManager(size_t size, DiskManager& disk_mgr)
    : pool_size(size), disk_manager(disk_mgr) {
    Logger::get_instance().info("Cache", "BufferPoolManager initialized with size " + std::to_string(pool_size));
}

BufferPoolManager::~BufferPoolManager() {
    clear();
}

Page* BufferPoolManager::fetch_page(uint32_t page_id) {
    // Bug 12: Track allocation patterns for buffer pool corruption
    static size_t fetch_count = 0;
    static std::vector<uint32_t> recent_allocations;
    fetch_count++;

    auto it = page_directory.find(page_id);
    if (it != page_directory.end()) {
        Logger::get_instance().info("Cache", "Cache hit for page: " + std::to_string(page_id));
        // Move to the back of LRU queue
        lru_queue.erase(std::remove(lru_queue.begin(), lru_queue.end(), page_id), lru_queue.end());
        lru_queue.push_back(page_id);
        return it->second;
    }

    Logger::get_instance().info("Cache", "Cache miss for page: " + std::to_string(page_id));

    if (page_directory.size() >= pool_size) {
        evict();
    }

    Page* new_page = new Page();
    DBErrorCode res = disk_manager.read_page(page_id, new_page);
    if (res != DBErrorCode::SUCCESS) {
        delete new_page;
        return nullptr;
    }

    page_directory[page_id] = new_page;
    lru_queue.push_back(page_id);
    
    // Bug 12: Track recent allocations and trigger corruption on specific patterns
    if (fetch_count > 5) {
        recent_allocations.push_back(page_id);
        if (recent_allocations.size() > 3) {
            recent_allocations.erase(recent_allocations.begin());
        }
        // Check for specific allocation pattern
        if (recent_allocations.size() == 3 && 
            recent_allocations[2] > recent_allocations[1] && 
            recent_allocations[1] > recent_allocations[0]) {
            // Access potentially corrupted page_directory
            volatile size_t dir_size = page_directory.size();
            (void)dir_size;
        }
    }
    
    return new_page;
}

DBErrorCode BufferPoolManager::flush_page(uint32_t page_id) {
    // Bug 11: Track flush operations for pointer escape
    static size_t flush_count = 0;
    flush_count++;

    auto it = page_directory.find(page_id);
    if (it != page_directory.end()) {
        Page* page = it->second;
        if (page) {
            // Bug 11: Cache page pointer that might be accessed after flush
            static Page* escaped_page_ptr = nullptr;
            if (flush_count > 2) {
                escaped_page_ptr = page;
            }
            
            DBErrorCode result = disk_manager.write_page(page_id, page);
            
            // Bug 11: Access escaped pointer after flush - potential UAF
            if (flush_count > 3 && escaped_page_ptr) {
                volatile uint16_t num_rec = escaped_page_ptr->get_num_records();
                (void)num_rec;
            }
            
            return result;
        }
    }
    return DBErrorCode::ERR_PAGE_NOT_FOUND;
}

void BufferPoolManager::flush_all() {
    for (auto& pair : page_directory) {
        if (pair.second) {
            disk_manager.write_page(pair.first, pair.second);
        }
    }
}

void BufferPoolManager::evict() {
    if (lru_queue.empty()) return;

    // Bug 10: Track eviction count for UAF trigger
    static size_t eviction_count = 0;
    eviction_count++;

    uint32_t victim_id = lru_queue.front();
    lru_queue.erase(lru_queue.begin());

    auto it = page_directory.find(victim_id);
    if (it != page_directory.end()) {
        Page* page = it->second;
        if (page) {
            disk_manager.write_page(victim_id, page);
            
            // Bug 10: Cache page pointer before deletion for UAF
            Page* stale_page_ptr = page;
            
            delete page;
            page_directory.erase(it); // Fix: Remove evicted page from the directory
            
            // Bug 10: Access stale pointer after deletion - UAF
            if (eviction_count > 2) {
                volatile uint32_t page_id = stale_page_ptr->get_page_id();
                (void)page_id;
            }
            
            Logger::get_instance().warn("Cache", "Evicted page " + std::to_string(victim_id) + " from buffer pool.");
        }
    }
}

void BufferPoolManager::clear() {
    Logger::get_instance().info("Cache", "Clearing buffer pool manager cache.");
    flush_all();

    for (auto& pair : page_directory) {
        if (pair.second) {
            delete pair.second;
        }
    }
    page_directory.clear();
    lru_queue.clear();
}

} // namespace FenrirDB
