#include "../src/cache.h"
#include <iostream>
#include <cassert>
#include <cstdio>

void test_cache_hits_and_misses() {
    std::cout << "Running test_cache_hits_and_misses..." << std::endl;
    std::string db_file = "test_cache.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(2, disk_mgr); // Small buffer pool of size 2

    uint32_t p0 = disk_mgr.allocate_page();
    uint32_t p1 = disk_mgr.allocate_page();
    uint32_t p2 = disk_mgr.allocate_page();

    // Fetch page 0 (cache miss, loaded from disk)
    FenrirDB::Page* page0 = cache_mgr.fetch_page(p0);
    assert(page0 != nullptr);
    assert(cache_mgr.get_size() == 1);

    // Fetch page 0 again (cache hit, retrieved from memory)
    FenrirDB::Page* page0_hit = cache_mgr.fetch_page(p0);
    assert(page0_hit == page0);
    assert(cache_mgr.get_size() == 1);

    // Fetch page 1 (cache miss)
    FenrirDB::Page* page1 = cache_mgr.fetch_page(p1);
    assert(page1 != nullptr);
    assert(cache_mgr.get_size() == 2);

    std::remove(db_file.c_str());
    std::cout << "test_cache_hits_and_misses passed." << std::endl;
}

void test_cache_lru_eviction() {
    std::cout << "Running test_cache_lru_eviction..." << std::endl;
    std::string db_file = "test_cache_lru.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(3, disk_mgr); // Pool size 3

    uint32_t p0 = disk_mgr.allocate_page();
    uint32_t p1 = disk_mgr.allocate_page();
    uint32_t p2 = disk_mgr.allocate_page();
    uint32_t p3 = disk_mgr.allocate_page();

    cache_mgr.fetch_page(p0);
    cache_mgr.fetch_page(p1);
    cache_mgr.fetch_page(p2);
    assert(cache_mgr.get_size() == 3);

    // Fetch page 3, should evict page 0 (the least recently used)
    cache_mgr.fetch_page(p3);
    assert(cache_mgr.get_size() == 3);

    // Fetch page 1, should update its LRU status
    cache_mgr.fetch_page(p1);

    std::remove(db_file.c_str());
    std::cout << "test_cache_lru_eviction passed." << std::endl;
}

int main() {
    test_cache_hits_and_misses();
    test_cache_lru_eviction();
    std::cout << "All Cache tests passed successfully!" << std::endl;
    return 0;
}
