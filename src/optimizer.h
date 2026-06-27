#ifndef FENRIRDB_OPTIMIZER_H
#define FENRIRDB_OPTIMIZER_H

#include <memory>
#include <string>
#include <unordered_map>
#include "sql_parser.h"
#include "query_planner.h"

namespace FenrirDB {

struct ColumnStats {
    int min_val = 0;
    int max_val = 0;
    size_t num_distinct = 0;
    size_t num_records = 0;
};

class TableStats {
private:
    std::unordered_map<std::string, ColumnStats> col_stats;
    size_t total_records = 0;

public:
    TableStats() = default;
    ~TableStats() = default;

    void add_column_stats(const std::string& col, const ColumnStats& stats);
    bool get_column_stats(const std::string& col, ColumnStats& stats) const;
    void set_total_records(size_t count) { total_records = count; }
    size_t get_total_records() const { return total_records; }

    double estimate_selectivity(const std::string& col, QueryOp op, const Variant& val) const;
};

class QueryOptimizer {
private:
    TableStats stats;

    double estimate_seq_scan_cost(size_t num_pages) const;
    double estimate_index_scan_cost(size_t index_depth, double selectivity, size_t total_records) const;

public:
    QueryOptimizer();
    ~QueryOptimizer() = default;

    TableStats& get_stats() { return stats; }

    // RBO: Pushdowns
    void optimize_pushdowns(SQLSelectStatement& stmt);

    // CBO: Decision to use Index Scan vs Seq Scan
    bool choose_index_scan(const std::string& table, const std::string& field, QueryOp op, const Variant& val,
                           size_t num_pages, size_t index_depth);
};

} // namespace FenrirDB

#endif // FENRIRDB_OPTIMIZER_H
