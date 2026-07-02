#include "query_planner.h"
#include "logger.h"
#include <algorithm>

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

    DBErrorCode res = index.search(CompositeKey(key), rid);
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
            // Apply expression evaluation for the scan predicate.
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

// ======================================================================
// NestedLoopJoinExecutor Implementation
// ======================================================================

NestedLoopJoinExecutor::NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> out, std::unique_ptr<AbstractExecutor> in,
                                               const std::string& out_c, const std::string& in_c)
    : outer(std::move(out)), inner(std::move(in)), outer_col(out_c), inner_col(in_c) {}

void NestedLoopJoinExecutor::init() {
    outer->init();
    inner->init();
    has_outer = false;
    Logger::get_instance().info("Executor", "NestedLoopJoin initialized.");
}

bool NestedLoopJoinExecutor::next(Document& doc, RecordID& rid) {
    while (true) {
        if (!has_outer) {
            if (!outer->next(outer_doc, outer_rid)) {
                return false; // Outer depleted
            }
            has_outer = true;
            inner->init(); // Restart inner scanner
        }

        Document inner_doc;
        RecordID inner_rid;
        while (inner->next(inner_doc, inner_rid)) {
            Variant out_val, in_val;
            if (outer_doc.get_field(outer_col, out_val) && inner_doc.get_field(inner_col, in_val)) {
                // Equality join check
                if (out_val.type == VariantType::INT && in_val.type == VariantType::INT) {
                    if (out_val.get_int() == in_val.get_int()) {
                        // Merge fields from outer and inner documents into doc
                        doc = outer_doc;
                        for (const auto& field : inner_doc.get_fields()) {
                            Variant existing;
                            if (!doc.get_field(field.first, existing)) {
                                doc.set_field(field.first, field.second);
                            }
                        }
                        rid = outer_rid;
                        return true;
                    }
                } else if (out_val.type == VariantType::STRING && in_val.type == VariantType::STRING) {
                    if (out_val.get_string() == in_val.get_string()) {
                        doc = outer_doc;
                        for (const auto& field : inner_doc.get_fields()) {
                            Variant existing;
                            if (!doc.get_field(field.first, existing)) {
                                doc.set_field(field.first, field.second);
                            }
                        }
                        rid = outer_rid;
                        return true;
                    }
                }
            }
        }
        has_outer = false; // Move to next outer record
    }
}

void NestedLoopJoinExecutor::close() {
    outer->close();
    inner->close();
}

// ======================================================================
// HashJoinExecutor Implementation
// ======================================================================

HashJoinExecutor::HashJoinExecutor(std::unique_ptr<AbstractExecutor> out, std::unique_ptr<AbstractExecutor> in,
                                   const std::string& out_c, const std::string& in_c)
    : outer(std::move(out)), inner(std::move(in)), outer_col(out_c), inner_col(in_c) {}

void HashJoinExecutor::build_hash_table() {
    hash_table.clear();
    inner->init();
    Document in_doc;
    RecordID in_rid;
    while (inner->next(in_doc, in_rid)) {
        Variant val;
        if (in_doc.get_field(inner_col, val)) {
            std::string hash_key;
            if (val.type == VariantType::INT) hash_key = std::to_string(val.get_int());
            else if (val.type == VariantType::STRING) hash_key = val.get_string();
            else if (val.type == VariantType::BOOL) hash_key = val.get_bool() ? "true" : "false";

            if (!hash_key.empty()) {
                hash_table[hash_key].push_back(in_doc);
            }
        }
    }
    inner->close();
}

void HashJoinExecutor::init() {
    outer->init();
    build_hash_table();
    cursor = 0;
    matched_docs.clear();
    Logger::get_instance().info("Executor", "HashJoin initialized. Built hash table size=" + std::to_string(hash_table.size()));
}

bool HashJoinExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < matched_docs.size()) {
        doc = matched_docs[cursor++];
        rid = { 0, 0 };
        return true;
    }

    matched_docs.clear();
    cursor = 0;

    Document out_doc;
    RecordID out_rid;
    while (outer->next(out_doc, out_rid)) {
        Variant val;
        if (out_doc.get_field(outer_col, val)) {
            std::string hash_key;
            if (val.type == VariantType::INT) hash_key = std::to_string(val.get_int());
            else if (val.type == VariantType::STRING) hash_key = val.get_string();
            else if (val.type == VariantType::BOOL) hash_key = val.get_bool() ? "true" : "false";

            auto it = hash_table.find(hash_key);
            if (it != hash_table.end()) {
                for (const auto& in_doc : it->second) {
                    Document joined = out_doc;
                    // Merge fields
                    for (const auto& field : in_doc.get_fields()) {
                        Variant existing;
                        if (!joined.get_field(field.first, existing)) {
                            joined.set_field(field.first, field.second);
                        }
                    }
                    matched_docs.push_back(joined);
                }
                if (!matched_docs.empty()) {
                    doc = matched_docs[cursor++];
                    rid = out_rid;
                    return true;
                }
            }
        }
    }
    return false;
}

void HashJoinExecutor::close() {
    outer->close();
}

// ======================================================================
// SortExecutor Implementation
// ======================================================================

SortExecutor::SortExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& col, bool asc)
    : child(std::move(ch)), sort_col(col), ascending(asc) {}

void SortExecutor::init() {
    child->init();
    sorted_records.clear();
    cursor = 0;

    Document doc;
    RecordID rid;
    while (child->next(doc, rid)) {
        sorted_records.push_back({ doc, rid });
    }

    // Sort documents using custom comparator
    std::sort(sorted_records.begin(), sorted_records.end(), [&](const std::pair<Document, RecordID>& a, const std::pair<Document, RecordID>& b) {
        Variant val_a, val_b;
        a.first.get_field(sort_col, val_a);
        b.first.get_field(sort_col, val_b);

        if (val_a.type == VariantType::INT && val_b.type == VariantType::INT) {
            return ascending ? (val_a.get_int() < val_b.get_int()) : (val_a.get_int() > val_b.get_int());
        } else if (val_a.type == VariantType::STRING && val_b.type == VariantType::STRING) {
            return ascending ? (val_a.get_string() < val_b.get_string()) : (val_a.get_string() > val_b.get_string());
        }
        return false;
    });

    Logger::get_instance().info("Executor", "SortExecutor sorted " + std::to_string(sorted_records.size()) + " records.");
}

bool SortExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < sorted_records.size()) {
        doc = sorted_records[cursor].first;
        rid = sorted_records[cursor].second;
        cursor++;
        return true;
    }
    return false;
}

void SortExecutor::close() {
    child->close();
}

// ======================================================================
// AggregationExecutor Implementation
// ======================================================================

AggregationExecutor::AggregationExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& col, AggType t, const std::string& grp)
    : child(std::move(ch)), agg_col(col), group_col(grp), type(t) {}

void AggregationExecutor::compute_aggregations() {
    agg_results.clear();
    child->init();

    // Map: GroupBy Key -> (Accumulator, Count)
    std::unordered_map<std::string, std::pair<int, int>> groups;

    Document doc;
    RecordID rid;
    while (child->next(doc, rid)) {
        std::string grp_key = "";
        if (!group_col.empty()) {
            Variant grp_val;
            if (doc.get_field(group_col, grp_val)) {
                if (grp_val.type == VariantType::STRING) grp_key = grp_val.get_string();
                else if (grp_val.type == VariantType::INT) grp_key = std::to_string(grp_val.get_int());
            }
        }

        Variant agg_val;
        int val = 0;
        if (doc.get_field(agg_col, agg_val) && agg_val.type == VariantType::INT) {
            val = agg_val.get_int();
        }

        if (groups.find(grp_key) == groups.end()) {
            groups[grp_key] = { val, 1 };
        } else {
            auto& p = groups[grp_key];
            p.second++; // Increment count
            if (type == AggType::SUM || type == AggType::AVG) {
                p.first += val;
            } else if (type == AggType::MIN) {
                p.first = std::min(p.first, val);
            } else if (type == AggType::MAX) {
                p.first = std::max(p.first, val);
            }
        }
    }
    child->close();

    // Create result documents
    for (const auto& pair : groups) {
        Document res_doc;
        if (!group_col.empty()) {
            res_doc.set_field(group_col, Variant(pair.first));
        }

        int final_val = 0;
        if (type == AggType::SUM) final_val = pair.second.first;
        else if (type == AggType::COUNT) final_val = pair.second.second;
        else if (type == AggType::MIN || type == AggType::MAX) final_val = pair.second.first;
        else if (type == AggType::AVG) {
            final_val = pair.second.second > 0 ? (pair.second.first / pair.second.second) : 0;
        }

        res_doc.set_field("result", Variant(final_val));
        agg_results.push_back(res_doc);
    }
}

void AggregationExecutor::init() {
    compute_aggregations();
    cursor = 0;
    Logger::get_instance().info("Executor", "AggregationExecutor computed aggregations. Groups count=" + std::to_string(agg_results.size()));
}

bool AggregationExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < agg_results.size()) {
        doc = agg_results[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void AggregationExecutor::close() {
    // Already closed child during compute_aggregations
}

// ======================================================================
// ProjectionExecutor Implementation
// ======================================================================

ProjectionExecutor::ProjectionExecutor(std::unique_ptr<AbstractExecutor> ch, const std::vector<std::string>& fields)
    : child(std::move(ch)), select_fields(fields) {}

void ProjectionExecutor::init() {
    child->init();
    Logger::get_instance().info("Executor", "ProjectionExecutor initialized.");
}

bool ProjectionExecutor::next(Document& doc, RecordID& rid) {
    Document child_doc;
    if (child->next(child_doc, rid)) {
        doc = Document(); // Clear doc
        for (const auto& field : select_fields) {
            Variant val;
            if (child_doc.get_field(field, val)) {
                doc.set_field(field, val);
            }
        }
        return true;
    }
    return false;
}

void ProjectionExecutor::close() {
    child->close();
}

// ======================================================================
// HavingExecutor Implementation
// ======================================================================

HavingExecutor::HavingExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& field, QueryOp o, const Variant& v)
    : child(std::move(ch)), agg_field(field), op(o), val(v) {}

void HavingExecutor::init() {
    child->init();
    Logger::get_instance().info("Executor", "HavingExecutor initialized.");
}

bool HavingExecutor::next(Document& doc, RecordID& rid) {
    while (child->next(doc, rid)) {
        Variant doc_val;
        if (doc.get_field(agg_field, doc_val)) {
            if (val.type == VariantType::INT && doc_val.type == VariantType::INT) {
                int left = doc_val.get_int();
                int right = val.get_int();
                if (op == QueryOp::EQ && left == right) return true;
                if (op == QueryOp::GT && left > right) return true;
                if (op == QueryOp::LT && left < right) return true;
            }
        }
    }
    return false;
}

void HavingExecutor::close() {
    child->close();
}

// ======================================================================
// DistinctExecutor Implementation
// ======================================================================

DistinctExecutor::DistinctExecutor(std::unique_ptr<AbstractExecutor> ch, const std::vector<std::string>& fields)
    : child(std::move(ch)), distinct_fields(fields) {}

void DistinctExecutor::build_unique_set() {
    unique_docs.clear();
    child->init();

    std::vector<std::string> seen_keys;

    Document doc;
    RecordID rid;
    while (child->next(doc, rid)) {
        std::string concat_key = "";
        for (const auto& field : distinct_fields) {
            Variant val;
            if (doc.get_field(field, val)) {
                if (val.type == VariantType::STRING) concat_key += val.get_string() + "#";
                else if (val.type == VariantType::INT) concat_key += std::to_string(val.get_int()) + "#";
            }
        }

        if (std::find(seen_keys.begin(), seen_keys.end(), concat_key) == seen_keys.end()) {
            seen_keys.push_back(concat_key);
            unique_docs.push_back(doc);
        }
    }
    child->close();
}

void DistinctExecutor::init() {
    build_unique_set();
    cursor = 0;
    Logger::get_instance().info("Executor", "DistinctExecutor initialized. Unique records count=" + std::to_string(unique_docs.size()));
}

bool DistinctExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < unique_docs.size()) {
        doc = unique_docs[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void DistinctExecutor::close() {
    // Already closed child during build_unique_set
}

} // namespace FenrirDB
