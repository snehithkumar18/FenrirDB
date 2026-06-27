#include "query_planner.h"
#include "logger.h"

namespace FenrirDB {

// ======================================================================
// SeqScanExecutor Implementation
// ======================================================================

SeqScanExecutor::SeqScanExecutor(DiskManager& dm, BufferPoolManager& cm)
    : disk_mgr(dm), cache_mgr(cm) {}

void SeqScanExecutor::init() {
    cursor_page = 1; // Page 0 is index page
    cursor_slot = 0;
    max_pages = disk_mgr.get_num_pages();
    Logger::get_instance().info("Executor", "SeqScan initialized. Pages to scan: " + std::to_string(max_pages));
}

bool SeqScanExecutor::next(Document& doc, RecordID& rid) {
    while (cursor_page < max_pages) {
        Page* page = cache_mgr.fetch_page(cursor_page);
        if (!page) {
            cursor_page++;
            cursor_slot = 0;
            continue;
        }

        uint16_t num_records = page->get_num_records();
        while (cursor_slot < num_records) {
            std::vector<uint8_t> bytes;
            if (page->get_record(cursor_slot, bytes) == DBErrorCode::SUCCESS) {
                doc = Document::deserialize(bytes);
                rid = { cursor_page, cursor_slot };
                cursor_slot++;
                return true;
            }
            cursor_slot++;
        }
        cursor_page++;
        cursor_slot = 0;
    }
    return false;
}

void SeqScanExecutor::close() {
    Logger::get_instance().info("Executor", "SeqScan complete.");
}

// ======================================================================
// IndexScanExecutor Implementation
// ======================================================================

IndexScanExecutor::IndexScanExecutor(BPlusTreeIndex& idx, BufferPoolManager& cm, const std::string& k)
    : index(idx), cache_mgr(cm), key(k) {}

void IndexScanExecutor::init() {
    fetched = false;
    Logger::get_instance().info("Executor", "IndexScan initialized on key: " + key);
}

bool IndexScanExecutor::next(Document& doc, RecordID& rid) {
    if (fetched) return false;

    DBErrorCode res = index.search(key, rid);
    if (res != DBErrorCode::SUCCESS) {
        fetched = true;
        return false;
    }

    Page* page = cache_mgr.fetch_page(rid.page_id);
    if (!page) {
        fetched = true;
        return false;
    }

    std::vector<uint8_t> bytes;
    res = page->get_record(rid.slot_id, bytes);
    if (res == DBErrorCode::SUCCESS) {
        doc = Document::deserialize(bytes);
        fetched = true;
        return true;
    }

    fetched = true;
    return false;
}

void IndexScanExecutor::close() {
    Logger::get_instance().info("Executor", "IndexScan complete.");
}

// ======================================================================
// FilterExecutor Implementation
// ======================================================================

FilterExecutor::FilterExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& f, QueryOp o, const Variant& v)
    : child(std::move(ch)), field(f), op(o), val(v) {}

void FilterExecutor::init() {
    child->init();
    Logger::get_instance().info("Executor", "FilterExecutor initialized on field: " + field);
}

bool FilterExecutor::next(Document& doc, RecordID& rid) {
    while (child->next(doc, rid)) {
        Variant doc_val;
        if (doc.get_field(field, doc_val)) {
            // Apply expression evaluation (Bug 3 Type Confusion can trigger here!)
            if (val.type == VariantType::INT) {
                int left = doc_val.get_int();
                int right = val.get_int();
                if (op == QueryOp::EQ && left == right) return true;
                if (op == QueryOp::GT && left > right) return true;
                if (op == QueryOp::LT && left < right) return true;
            } else if (val.type == VariantType::STRING) {
                std::string left = doc_val.get_string();
                std::string right = val.get_string();
                if (op == QueryOp::EQ && left == right) return true;
            } else if (val.type == VariantType::BOOL) {
                bool left = doc_val.get_bool();
                bool right = val.get_bool();
                if (op == QueryOp::EQ && left == right) return true;
            }
        }
    }
    return false;
}

void FilterExecutor::close() {
    child->close();
    Logger::get_instance().info("Executor", "FilterExecutor complete.");
}

// ======================================================================
// LimitExecutor Implementation
// ======================================================================

LimitExecutor::LimitExecutor(std::unique_ptr<AbstractExecutor> ch, size_t lim)
    : child(std::move(ch)), limit(lim) {}

void LimitExecutor::init() {
    child->init();
    count = 0;
    Logger::get_instance().info("Executor", "LimitExecutor initialized with limit " + std::to_string(limit));
}

bool LimitExecutor::next(Document& doc, RecordID& rid) {
    if (count >= limit) return false;
    if (child->next(doc, rid)) {
        count++;
        return true;
    }
    return false;
}

void LimitExecutor::close() {
    child->close();
}

// ======================================================================
// QueryPlanner Implementation
// ======================================================================

QueryPlanner::QueryPlanner(DiskManager& dm, BufferPoolManager& cm, BPlusTreeIndex& idx)
    : disk_mgr(dm), cache_mgr(cm), index(idx) {}

std::unique_ptr<AbstractExecutor> QueryPlanner::plan_query(const SQLSelectStatement& stmt) {
    std::unique_ptr<AbstractExecutor> leaf_executor;

    // Optimizer heuristic: if filter is equality on key field name/id, use B+ Tree index scan
    if (!stmt.where_field.empty() && stmt.where_field == "id" && stmt.where_op == QueryOp::EQ) {
        leaf_executor = std::make_unique<IndexScanExecutor>(index, cache_mgr, stmt.where_value.get_string());
    } else {
        // Fall back to sequential scan
        leaf_executor = std::make_unique<SeqScanExecutor>(disk_mgr, cache_mgr);
    }

    // Add filter if WHERE clause exists and is not optimized to index scan
    if (!stmt.where_field.empty() && !(stmt.where_field == "id" && stmt.where_op == QueryOp::EQ)) {
        return std::make_unique<FilterExecutor>(std::move(leaf_executor), stmt.where_field, stmt.where_op, stmt.where_value);
    }

    return leaf_executor;
}

} // namespace FenrirDB
