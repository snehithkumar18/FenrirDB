#ifndef FENRIRDB_STATISTICS_H
#define FENRIRDB_STATISTICS_H

#include "query.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace FenrirDB {

class Histogram {
private:
    std::vector<double> buckets;
    std::vector<double> frequencies;
    double total_count;

public:
    Histogram() : total_count(0.0) {}
    Histogram(const std::vector<double>& b, const std::vector<double>& f);

    double estimate_selectivity(QueryOp op, const Variant& val) const;
    void add_value(double val);
    void build_equi_depth(const std::vector<double>& values, size_t num_buckets);
};

struct ColumnStats {
    double num_distinct;
    double num_nulls;
    double min_val;
    double max_val;
    Histogram histogram;
};

class TableStatistics {
private:
    std::string table_name;
    size_t total_rows;
    std::unordered_map<std::string, ColumnStats> col_stats;

public:
    TableStatistics(const std::string& name);
    
    void update_stats(const std::vector<Document>& docs);
    double get_selectivity(const std::string& col, QueryOp op, const Variant& val) const;
    size_t get_total_rows() const { return total_rows; }
    
    const std::unordered_map<std::string, ColumnStats>& get_column_stats() const { return col_stats; }
};

} // namespace FenrirDB

#endif // FENRIRDB_STATISTICS_H
