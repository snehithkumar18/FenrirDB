#ifndef FENRIRDB_STORAGE_H
#define FENRIRDB_STORAGE_H

#include <cstdint>
#include <vector>
#include <string>
#include "errors.h"

namespace FenrirDB {

constexpr uint16_t PAGE_SIZE = 4096;

struct Slot {
    uint16_t offset = 0;
    uint16_t length = 0;
};

class Page {
public:
    uint8_t data[PAGE_SIZE];

    Page();
    explicit Page(uint32_t page_id);

    uint32_t get_page_id() const;
    uint16_t get_num_records() const;
    uint16_t get_free_space() const;

    DBErrorCode insert_record(uint16_t slot_id, const uint8_t* record_data, uint16_t record_len);
    DBErrorCode get_record(uint16_t slot_id, std::vector<uint8_t>& record_data) const;
    DBErrorCode delete_record(uint16_t slot_id);
    DBErrorCode update_record(uint16_t slot_id, const uint8_t* record_data, uint16_t record_len);

    void compact();
};

class DiskManager {
private:
    std::string db_filename;
    uint32_t num_pages = 0;

public:
    explicit DiskManager(const std::string& filename);
    ~DiskManager() = default;

    DBErrorCode read_page(uint32_t page_id, Page* page);
    DBErrorCode write_page(uint32_t page_id, const Page* page);
    uint32_t allocate_page();
    uint32_t get_num_pages() const { return num_pages; }
};

} // namespace FenrirDB

#endif // FENRIRDB_STORAGE_H
