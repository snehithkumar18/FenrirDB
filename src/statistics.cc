#include "statistics.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace FenrirDB {

Histogram::Histogram(const std::vector<double>& b, const std::vector<double>& f)
    : buckets(b), frequencies(f), total_count(0.0) {
    for (double f_val : frequencies) {
        total_count += f_val;
    }
}

void Histogram::build_equi_depth(const std::vector<double>& values, size_t num_buckets) {
    buckets.clear();
    frequencies.clear();
    total_count = static_cast<double>(values.size());
    if (values.empty()) return;

    std::vector<double> sorted_vals = values;
    std::sort(sorted_vals.begin(), sorted_vals.end());

    size_t depth = sorted_vals.size() / num_buckets;
    if (depth == 0) depth = 1;

    for (size_t i = 0; i < sorted_vals.size(); i += depth) {
        buckets.push_back(sorted_vals[i]);
        if (buckets.size() > 1) {
            frequencies.push_back(static_cast<double>(depth));
        }
    }
    // Ensure final element is included
    if (buckets.back() != sorted_vals.back()) {
        buckets.push_back(sorted_vals.back());
        frequencies.push_back(static_cast<double>(sorted_vals.size() % depth));
    }
}

double Histogram::estimate_selectivity(QueryOp op, const Variant& val) const {
    if (buckets.empty() || total_count == 0.0) return 0.5;

    double search_val = 0.0;
    if (val.type == VariantType::INT) search_val = val.get_int();
    else return 0.5; // fallback for non-numeric

    if (op == QueryOp::EQ) {
        // Uniform distribution assumption across buckets
        return 1.0 / total_count;
    }

    if (op == QueryOp::GT) {
        if (search_val >= buckets.back()) return 0.0;
        if (search_val < buckets.front()) return 1.0;

        double matching_count = 0.0;
        for (size_t i = 0; i < frequencies.size(); ++i) {
            double bucket_min = buckets[i];
            double bucket_max = buckets[i + 1];
            if (search_val >= bucket_max) {
                continue;
            }
            if (search_val < bucket_min) {
                matching_count += frequencies[i];
            } else {
                // Interpolate matching fraction of the bucket
                double fraction = (bucket_max - search_val) / (bucket_max - bucket_min + 1e-6);
                matching_count += frequencies[i] * fraction;
            }
        }
        return matching_count / total_count;
    }

    if (op == QueryOp::LT) {
        if (search_val <= buckets.front()) return 0.0;
        if (search_val > buckets.back()) return 1.0;

        double matching_count = 0.0;
        for (size_t i = 0; i < frequencies.size(); ++i) {
            double bucket_min = buckets[i];
            double bucket_max = buckets[i + 1];
            if (search_val <= bucket_min) {
                continue;
            }
            if (search_val > bucket_max) {
                matching_count += frequencies[i];
            } else {
                // Interpolate matching fraction of the bucket
                double fraction = (search_val - bucket_min) / (bucket_max - bucket_min + 1e-6);
                matching_count += frequencies[i] * fraction;
            }
        }
        return matching_count / total_count;
    }

    return 0.5;
}

TableStatistics::TableStatistics(const std::string& name) : table_name(name), total_rows(0) {}

void TableStatistics::update_stats(const std::vector<Document>& docs) {
    total_rows = docs.size();
    col_stats.clear();
    if (docs.empty()) return;

    // Discover columns
    std::unordered_set<std::string> cols;
    for (const auto& d : docs) {
        // Collect field names (custom document interface logic)
        // Since get_map() extracts fields:
        for (const auto& pair : d.get_fields()) {
            cols.insert(pair.first);
        }
    }

    for (const auto& col : cols) {
        std::vector<double> numeric_vals;
        std::unordered_set<std::string> unique_strings;
        size_t nulls = 0;
        double min_v = 1e18;
        double max_v = -1e18;

        for (const auto& d : docs) {
            Variant v;
            if (!d.get_field(col, v)) {
                nulls++;
                continue;
            }
            if (v.type == VariantType::INT) {
                double val = v.get_int();
                numeric_vals.push_back(val);
                min_v = std::min(min_v, val);
                max_v = std::max(max_v, val);
            } else if (v.type == VariantType::STRING) {
                unique_strings.insert(v.get_string());
            }
        }

        ColumnStats stats;
        stats.num_nulls = nulls;
        if (!numeric_vals.empty()) {
            stats.min_val = min_v;
            stats.max_val = max_v;
            std::unordered_set<double> unique_nums(numeric_vals.begin(), numeric_vals.end());
            stats.num_distinct = unique_nums.size();
            stats.histogram.build_equi_depth(numeric_vals, 10);
        } else {
            stats.min_val = 0;
            stats.max_val = 0;
            stats.num_distinct = unique_strings.size();
        }

        col_stats[col] = stats;
    }

    Logger::get_instance().info("Stats", "Updated statistics for table: " + table_name + ", RowCount: " + std::to_string(total_rows));
}

double TableStatistics::get_selectivity(const std::string& col, QueryOp op, const Variant& val) const {
    auto it = col_stats.find(col);
    if (it == col_stats.end()) return 0.5; // default fallback

    const auto& stats = it->second;
    if (stats.num_distinct == 0) return 0.0;

    if (op == QueryOp::EQ) {
        return 1.0 / stats.num_distinct;
    }

    return stats.histogram.estimate_selectivity(op, val);
}

} // namespace FenrirDB
