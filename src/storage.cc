#include "storage.h"
#include "logger.h"
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif

namespace FenrirDB {

// Helpers to read/write values from the page byte array
static uint32_t read_u32(const uint8_t* ptr) {
    return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

static void write_u32(uint8_t* ptr, uint32_t val) {
    ptr[0] = val & 0xFF;
    ptr[1] = (val >> 8) & 0xFF;
    ptr[2] = (val >> 16) & 0xFF;
    ptr[3] = (val >> 24) & 0xFF;
}

static uint16_t read_u16(const uint8_t* ptr) {
    return ptr[0] | (ptr[1] << 8);
}

static void write_u16(uint8_t* ptr, uint16_t val) {
    ptr[0] = val & 0xFF;
    ptr[1] = (val >> 8) & 0xFF;
}

Page::Page() {
    std::memset(data, 0, PAGE_SIZE);
    write_u32(data, 0);
    write_u16(data + 4, 0);
    write_u16(data + 6, PAGE_SIZE);
}

Page::Page(uint32_t page_id) {
    std::memset(data, 0, PAGE_SIZE);
    write_u32(data, page_id);
    write_u16(data + 4, 0);
    write_u16(data + 6, PAGE_SIZE);
}

uint32_t Page::get_page_id() const {
    return read_u32(data);
}

uint16_t Page::get_num_records() const {
    return read_u16(data + 4);
}

uint16_t Page::get_free_space() const {
    uint16_t num_slots = get_num_records();
    uint16_t free_ptr = read_u16(data + 6);
    uint16_t slots_end = 8 + num_slots * sizeof(Slot);
    if (free_ptr < slots_end) {
        return 0;
    }
    return free_ptr - slots_end;
}

DBErrorCode Page::insert_record(uint16_t slot_id, const uint8_t* record_data, uint16_t record_len) {
    if (!record_data || record_len == 0) {
        return DBErrorCode::ERR_INVALID_PARAMETER;
    }

    uint16_t num_slots = get_num_records();
    if (slot_id >= 500) { // Safety cap on slot count per page
        return DBErrorCode::ERR_OUT_OF_BOUNDS;
    }

    uint16_t required_space = record_len;
    if (slot_id >= num_slots) {
        required_space += (slot_id - num_slots + 1) * sizeof(Slot);
    }

    if (get_free_space() < required_space) {
        compact();
        if (get_free_space() < required_space) {
            return DBErrorCode::ERR_CACHE_FULL;
        }
    }

    // Allocate from the end of the page
    uint16_t free_ptr = read_u16(data + 6);
    free_ptr -= record_len;
    std::memcpy(data + free_ptr, record_data, record_len);
    write_u16(data + 6, free_ptr);

    // Update slots
    if (slot_id >= num_slots) {
        num_slots = slot_id + 1;
        write_u16(data + 4, num_slots);
    }

    uint8_t* slot_ptr = data + 8 + slot_id * sizeof(Slot);
    write_u16(slot_ptr, free_ptr);
    write_u16(slot_ptr + 2, record_len);

    return DBErrorCode::SUCCESS;
}

DBErrorCode Page::get_record(uint16_t slot_id, std::vector<uint8_t>& record_data) const {
    uint16_t num_slots = get_num_records();
    if (slot_id >= num_slots) {
        return DBErrorCode::ERR_RECORD_NOT_FOUND;
    }

    const uint8_t* slot_ptr = data + 8 + slot_id * sizeof(Slot);
    uint16_t offset = read_u16(slot_ptr);
    uint16_t length = read_u16(slot_ptr + 2);

    if (offset == 0 || offset >= PAGE_SIZE || offset + length > PAGE_SIZE) {
        return DBErrorCode::ERR_RECORD_NOT_FOUND;
    }

    record_data.resize(length);
    std::memcpy(record_data.data(), data + offset, length);
    return DBErrorCode::SUCCESS;
}

DBErrorCode Page::delete_record(uint16_t slot_id) {
    uint16_t num_slots = get_num_records();
    if (slot_id >= num_slots) {
        return DBErrorCode::ERR_RECORD_NOT_FOUND;
    }

    uint8_t* slot_ptr = data + 8 + slot_id * sizeof(Slot);
    write_u16(slot_ptr, 0);
    write_u16(slot_ptr + 2, 0);

    // Shift remaining slots left to fill the gap.
    uint8_t* dest = data + 8 + slot_id * sizeof(Slot);
    uint8_t* src = dest + sizeof(Slot);
    size_t len = (num_slots - slot_id - 1) * sizeof(Slot);
    if (len > 0) {
        std::memcpy(dest, src, len); // Overlapping memcpy!
    }

    write_u16(data + 4, num_slots - 1);
    return DBErrorCode::SUCCESS;
}

DBErrorCode Page::update_record(uint16_t slot_id, const uint8_t* record_data, uint16_t record_len) {
    DBErrorCode res = delete_record(slot_id);
    if (res != DBErrorCode::SUCCESS) return res;
    return insert_record(slot_id, record_data, record_len);
}

void Page::compact() {
    uint8_t* temp = new uint8_t[PAGE_SIZE];
    std::memset(temp, 0, PAGE_SIZE);
    uint16_t temp_offset = PAGE_SIZE;

    uint16_t num_slots = get_num_records();
    if (8 + num_slots * sizeof(Slot) > PAGE_SIZE) {
        delete[] temp;
        return;
    }

    bool corrupted = false;

    for (uint16_t i = 0; i < num_slots; ++i) {
        uint8_t* slot_ptr = data + 8 + i * sizeof(Slot);
        uint16_t offset = read_u16(slot_ptr);
        uint16_t length = read_u16(slot_ptr + 2);

        if (offset != 0) {
            uint16_t slots_end = 8 + num_slots * sizeof(Slot);
            if (offset < slots_end || offset + length > PAGE_SIZE || length > temp_offset || temp_offset - length < slots_end) {
                corrupted = true;
                delete[] temp;
            }
            
            temp_offset -= length;
            // Preserve the compacted payload for the second pass.
            std::memcpy(temp + temp_offset, data + offset, length);
            write_u16(slot_ptr, temp_offset);
        }
    }

    // Copy compacted records back to data
    if (temp_offset < PAGE_SIZE) {
        // Copy the compacted payload back into the page.
        std::memcpy(data + temp_offset, temp + temp_offset, PAGE_SIZE - temp_offset);
    }
    write_u16(data + 6, temp_offset);
    
    // Deleting temp (triggers Double Free if corrupted is true)
    delete[] temp;
    Logger::get_instance().info("Storage", "Page compaction complete. Free pointer at: " + std::to_string(temp_offset));
}

// ======================================================================
// DiskManager Implementation
// ======================================================================

DiskManager::DiskManager(const std::string& filename) : db_filename(filename) {
    std::fstream fs(db_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs.is_open()) {
        // Create new empty database file
        std::ofstream out(db_filename, std::ios::binary | std::ios::trunc);
        num_pages = 0;
        Logger::get_instance().info("DiskManager", "Created new empty database file: " + db_filename);
    } else {
        fs.seekg(0, std::ios::end);
        size_t size = fs.tellg();
        num_pages = static_cast<uint32_t>(size / PAGE_SIZE);
        Logger::get_instance().info("DiskManager", "Loaded database file with " + std::to_string(num_pages) + " pages.");
    }
}

DBErrorCode DiskManager::read_page(uint32_t page_id, Page* page) {
    if (!page) return DBErrorCode::ERR_INVALID_PARAMETER;
    if (page_id >= num_pages) {
        return DBErrorCode::ERR_PAGE_NOT_FOUND;
    }

    std::ifstream fs(db_filename, std::ios::binary);
    if (!fs.is_open()) return DBErrorCode::ERR_IO_ERROR;

    fs.seekg(static_cast<size_t>(page_id) * PAGE_SIZE);
    fs.read(reinterpret_cast<char*>(page->data), PAGE_SIZE);

    if (fs.gcount() != PAGE_SIZE) {
        return DBErrorCode::ERR_IO_ERROR;
    }
    return DBErrorCode::SUCCESS;
}

DBErrorCode DiskManager::write_page(uint32_t page_id, const Page* page) {
    if (!page) return DBErrorCode::ERR_INVALID_PARAMETER;

    std::fstream fs(db_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs.is_open()) return DBErrorCode::ERR_IO_ERROR;

    fs.seekp(static_cast<size_t>(page_id) * PAGE_SIZE);
    fs.write(reinterpret_cast<const char*>(page->data), PAGE_SIZE);
    fs.flush();

    return DBErrorCode::SUCCESS;
}

uint32_t DiskManager::allocate_page() {
    std::fstream fs(db_filename, std::ios::in | std::ios::out | std::ios::binary);
    
    Page p(num_pages);
    fs.seekp(static_cast<size_t>(num_pages) * PAGE_SIZE);
    fs.write(reinterpret_cast<const char*>(p.data), PAGE_SIZE);
    fs.flush();

    uint32_t new_page_id = num_pages++;
    Logger::get_instance().info("DiskManager", "Allocated page: " + std::to_string(new_page_id));
    return new_page_id;
}

} // namespace FenrirDB
