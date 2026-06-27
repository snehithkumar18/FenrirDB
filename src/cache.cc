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
    auto it = page_directory.find(page_id);
    if (it != page_directory.end()) {
        Logger::get_instance().info("Cache", "Cache hit for page: " + std::to_string(page_id));
        // Move to the back of LRU queue
        lru_queue.erase(std::remove(lru_queue.begin(), lru_queue.end(), page_id), lru_queue.end());
        lru_queue.push_back(page_id);
        return it->second; // DELIBERATE UAF: Might return a freed page pointer
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
    return new_page;
}

DBErrorCode BufferPoolManager::flush_page(uint32_t page_id) {
    auto it = page_directory.find(page_id);
    if (it != page_directory.end()) {
        Page* page = it->second;
        if (page) {
            return disk_manager.write_page(page_id, page);
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

    uint32_t victim_id = lru_queue.front();
    lru_queue.erase(lru_queue.begin());

    auto it = page_directory.find(victim_id);
    if (it != page_directory.end()) {
        Page* page = it->second;
        if (page) {
            disk_manager.write_page(victim_id, page);
            delete page; // Freeing memory
            
            // DELIBERATE BUG: We do not erase the victim_id from page_directory!
            // This leaves the pointer dangling inside the map.
            Logger::get_instance().warn("Cache", "Evicted page " + std::to_string(victim_id) + " but retained key in directory.");
        }
    }
}

void BufferPoolManager::clear() {
    Logger::get_instance().info("Cache", "Clearing buffer pool manager cache.");
    flush_all();

    // DELIBERATE BUG: Double Free
    // If evict() has deleted pages, the pointer is still inside page_directory.
    // Iterating over page_directory and calling delete again triggers double free.
    for (auto& pair : page_directory) {
        if (pair.second) {
            delete pair.second;
        }
    }
    page_directory.clear();
    lru_queue.clear();
}

} // namespace FenrirDB
