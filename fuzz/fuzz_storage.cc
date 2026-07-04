#include "../src/database.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <cstdio>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 20) return 0;

    // Part 1: exercise page compaction. (Bug 16-17)
    {
        FenrirDB::Page page;
        size_t copy_len = (size < FenrirDB::PAGE_SIZE) ? size : FenrirDB::PAGE_SIZE;
        std::memcpy(page.data, data, copy_len);
        page.compact();
    }
    }

    // Part 2: exercise cache eviction and refetch behavior when the input
    // describes a small page-touch sequence.
    if (size >= 32 && (data[0] & 0x07) >= 4 && (data[1] & 0x03) == 3) {
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
        
        // Fetch 4th page to force an LRU replacement.
        cache_mgr.fetch_page(p3);

        // Fetch page 0 again after an eviction/refetch cycle.
        FenrirDB::Page* evicted = cache_mgr.fetch_page(p0);
        if (evicted) {
            // Touch the page metadata after the refetch.
            evicted->get_num_records();
        }

        // Clear the cache before closing the backing file.
        cache_mgr.clear();
    }
    std::remove("fuzz_storage.db");

    // Part 3: exercise B+ tree insert and split paths for inputs that look
    // like an insertion stream rather than a raw page image.
    if (size >= 64 && (data[2] & 0x0f) >= 8 && (data[3] ^ data[4]) != 0) {
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

