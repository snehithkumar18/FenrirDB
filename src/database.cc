#include "database.h"
#include "logger.h"

namespace FenrirDB {

DBErrorCode Database::open(const std::string& filepath) {
    disk_manager = std::make_unique<DiskManager>(filepath);
    cache_manager = std::make_unique<BufferPoolManager>(10, *disk_manager); // Pool size of 10 pages

    if (disk_manager->get_num_pages() == 0) {
        // Allocate page 0 as B+ Tree index root page
        uint32_t root_id = disk_manager->allocate_page();
        Page* root_page = cache_manager->fetch_page(root_id);
        if (root_page) {
            IndexNode* node = reinterpret_cast<IndexNode*>(root_page->data + 8);
            new (node) IndexNode(); // Placement new to initialize IndexNode structure
            node->is_leaf = true;
            node->num_keys = 0;
            cache_manager->flush_page(root_id);
        }
        root_index_page = root_id;
    } else {
        root_index_page = 0;
    }

    index = std::make_unique<BPlusTreeIndex>(*disk_manager, *cache_manager, root_index_page);
    Logger::get_instance().info("Database", "Opened database file: " + filepath);
    return DBErrorCode::SUCCESS;
}

void Database::close() {
    if (cache_manager) {
        cache_manager->flush_all();
        cache_manager->clear();
    }
    index.reset();
    cache_manager.reset();
    disk_manager.reset();
    Logger::get_instance().info("Database", "Database closed successfully.");
}

DBErrorCode Database::insert(const std::string& key, const Document& doc) {
    if (!disk_manager || !cache_manager || !index) {
        return DBErrorCode::ERR_GENERIC;
    }

    std::vector<uint8_t> bytes = doc.serialize();
    if (bytes.empty()) {
        return DBErrorCode::ERR_INVALID_PARAMETER;
    }

    // Allocate a new page for document record
    uint32_t doc_page_id = disk_manager->allocate_page();
    Page* page = cache_manager->fetch_page(doc_page_id);
    if (!page) {
        return DBErrorCode::ERR_PAGE_NOT_FOUND;
    }

    new (page) Page(doc_page_id);

    uint16_t slot_id = 0; // Insert into slot 0
    DBErrorCode res = page->insert_record(slot_id, bytes.data(), static_cast<uint16_t>(bytes.size()));
    if (res != DBErrorCode::SUCCESS) {
        return res;
    }

    cache_manager->flush_page(doc_page_id);

    // Insert record pointer location into B+ Tree index
    RecordID val = { doc_page_id, slot_id };
    res = index->insert(key, val);
    
    Logger::get_instance().info("Database", "Inserted document under key: " + key + " at Page=" + std::to_string(doc_page_id));
    return res;
}

DBErrorCode Database::get(const std::string& key, Document& doc) {
    if (!cache_manager || !index) return DBErrorCode::ERR_GENERIC;

    RecordID loc;
    DBErrorCode res = index->search(key, loc);
    if (res != DBErrorCode::SUCCESS) {
        return res;
    }

    Page* page = cache_manager->fetch_page(loc.page_id);
    if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;

    std::vector<uint8_t> record_bytes;
    res = page->get_record(loc.slot_id, record_bytes);
    if (res != DBErrorCode::SUCCESS) {
        return res;
    }

    doc = Document::deserialize(record_bytes);
    return DBErrorCode::SUCCESS;
}

DBErrorCode Database::find_range(const std::string& start_key, const std::string& end_key, std::vector<Document>& results) {
    if (!cache_manager || !index) return DBErrorCode::ERR_GENERIC;

    std::vector<RecordID> locs;
    DBErrorCode res = index->range_search(start_key, end_key, locs);
    if (res != DBErrorCode::SUCCESS) {
        return res;
    }

    for (const auto& loc : locs) {
        Page* page = cache_manager->fetch_page(loc.page_id);
        if (!page) continue;

        std::vector<uint8_t> record_bytes;
        if (page->get_record(loc.slot_id, record_bytes) == DBErrorCode::SUCCESS) {
            results.push_back(Document::deserialize(record_bytes));
        }
    }
    return DBErrorCode::SUCCESS;
}

DBErrorCode Database::query(const std::string& query_str, std::vector<Document>& results) {
    if (!disk_manager || !cache_manager) return DBErrorCode::ERR_GENERIC;

    QueryNode query_node = QueryEvaluator::parse_query_string(query_str);
    if (query_node.field.empty()) {
        return DBErrorCode::ERR_INVALID_PARAMETER;
    }

    uint32_t num_pages = disk_manager->get_num_pages();
    for (uint32_t page_id = 0; page_id < num_pages; ++page_id) {
        // Index page is page 0, skip it
        if (page_id == root_index_page) continue;

        Page* page = cache_manager->fetch_page(page_id);
        if (!page) continue;

        uint16_t num_records = page->get_num_records();
        for (uint16_t slot_id = 0; slot_id < num_records; ++slot_id) {
            std::vector<uint8_t> record_bytes;
            if (page->get_record(slot_id, record_bytes) == DBErrorCode::SUCCESS) {
                Document doc = Document::deserialize(record_bytes);
                if (QueryEvaluator::evaluate(doc, query_node)) {
                    results.push_back(doc);
                }
            }
        }
    }
    return DBErrorCode::SUCCESS;
}

} // namespace FenrirDB
