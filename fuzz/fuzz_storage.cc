#include "../src/database.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <cstdio>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 20) return 0;

    // Part 1: Fuzz Page Compaction (Bug 1 - Heap Overflow / Underflow)
    {
        FenrirDB::Page page;
        size_t copy_len = (size < FenrirDB::PAGE_SIZE) ? size : FenrirDB::PAGE_SIZE;
        std::memcpy(page.data, data, copy_len);
        page.compact();
    }

    // Part 2: Fuzz Cache Eviction & double free (Bug 2)
    {
        FenrirDB::DiskManager disk_mgr("fuzz_storage.db");
        FenrirDB::BufferPoolManager cache_mgr(3, disk_mgr); // Buffer pool size 3

        // Allocate pages
        uint32_t p0 = disk_mgr.allocate_page();
        uint32_t p1 = disk_mgr.allocate_page();
        uint32_t p2 = disk_mgr.allocate_page();
        uint32_t p3 = disk_mgr.allocate_page();

        // Fetch pages to fill cache
        cache_mgr.fetch_page(p0);
        cache_mgr.fetch_page(p1);
        cache_mgr.fetch_page(p2);
        
        // Fetch 4th page to trigger eviction of p0 (LRU)
        cache_mgr.fetch_page(p3);

        // Fetch page 0 again (accesses dangling pointer)
        FenrirDB::Page* evicted = cache_mgr.fetch_page(p0);
        if (evicted) {
            // Read or write from evicted page (triggers UAF)
            evicted->get_num_records();
        }

        // Trigger cache clear (triggers double free of page 0 pointer)
        cache_mgr.clear();
    }
    std::remove("fuzz_storage.db");

    // Part 3: Fuzz B+ Tree Split Cursor UAF (Bug 1)
    {
        FenrirDB::DiskManager disk_mgr("fuzz_btree.db");
        FenrirDB::BufferPoolManager cache_mgr(3, disk_mgr); // Buffer pool size 3
        
        uint32_t root_id = disk_mgr.allocate_page();
        FenrirDB::BPlusTreeIndex btree(disk_mgr, cache_mgr, root_id);

        size_t offset = 0;
        int insert_count = 0;
        while (offset + 4 < size && insert_count < 100) {
            uint32_t key_val = data[offset] | (data[offset+1] << 8);
            uint32_t page_val = data[offset+2] | (data[offset+3] << 8);
            offset += 4;

            FenrirDB::CompositeKey key(std::to_string(key_val));
            FenrirDB::RecordID rid{page_val, static_cast<uint16_t>(key_val % 10)};
            
            btree.insert(key, rid);
            insert_count++;
        }
    }
    std::remove("fuzz_btree.db");

    return 0;
}
