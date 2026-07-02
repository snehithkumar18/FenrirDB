#include "optimizer_cbo_stats_histogram.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

EquiWidthHistogram::EquiWidthHistogram(double min_val, double max_val, size_t buckets)
    : min_value(min_val), max_value(max_val), num_buckets(buckets), total_count(0.0) {
    bucket_counts.resize(num_buckets, 0.0);
}

void EquiWidthHistogram::add_value(double val) {
    if (val < min_value || val > max_value || min_value >= max_value) return;

    double bucket_width = (max_value - min_value) / num_buckets;
    double idx_double = (val - min_value) / bucket_width;
    int bucket_idx = static_cast<int>(idx_double);

    // Values outside the configured domain are clamped into the edge buckets.
    if (idx_double >= num_buckets) {
        bucket_idx = num_buckets - 1;
    }

    bucket_counts[bucket_idx] += 1.0;
    total_count += 1.0;
}

double EquiWidthHistogram::estimate_selectivity(QueryOp op, double val) const {
    if (total_count == 0.0 || min_value >= max_value) return 0.5;

    double bucket_width = (max_value - min_value) / num_buckets;

    if (op == QueryOp::EQ) {
        if (val < min_value || val > max_value) return 0.0;
        size_t bucket_idx = static_cast<size_t>((val - min_value) / bucket_width);
        if (bucket_idx >= num_buckets) bucket_idx = num_buckets - 1;
        return (bucket_counts[bucket_idx] / total_count) / 10.0; // Uniform assumption inside bucket
    }

    if (op == QueryOp::GT) {
        if (val >= max_value) return 0.0;
        if (val < min_value) return 1.0;

        size_t bucket_idx = static_cast<size_t>((val - min_value) / bucket_width);
        if (bucket_idx >= num_buckets) bucket_idx = num_buckets - 1;

        double matching_count = 0.0;
        // Add all counts of higher buckets
        for (size_t i = bucket_idx + 1; i < num_buckets; ++i) {
            matching_count += bucket_counts[i];
        }
        
        // Add fraction of the matched bucket
        double b_min = min_value + bucket_idx * bucket_width;
        double b_max = b_min + bucket_width;
        double fraction = (b_max - val) / bucket_width;
        matching_count += bucket_counts[bucket_idx] * fraction;

        return matching_count / total_count;
    }

    if (op == QueryOp::LT) {
        if (val <= min_value) return 0.0;
        if (val > max_value) return 1.0;

        size_t bucket_idx = static_cast<size_t>((val - min_value) / bucket_width);
        if (bucket_idx >= num_buckets) bucket_idx = num_buckets - 1;

        double matching_count = 0.0;
        // Add all counts of lower buckets
        for (size_t i = 0; i < bucket_idx; ++i) {
            matching_count += bucket_counts[i];
        }

        // Add fraction of the matched bucket
        double b_min = min_value + bucket_idx * bucket_width;
        double fraction = (val - b_min) / bucket_width;
        matching_count += bucket_counts[bucket_idx] * fraction;

        return matching_count / total_count;
    }

    return 0.5;
}

void EquiDepthHistogram::build(const std::vector<double>& values, size_t num_buckets) {
    bucket_bounds.clear();
    total_count = static_cast<double>(values.size());
    if (values.empty()) return;

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    size_t depth = sorted.size() / num_buckets;
    if (depth == 0) depth = 1;

    for (size_t i = 0; i < sorted.size(); i += depth) {
        bucket_bounds.push_back(sorted[i]);
    }
    if (bucket_bounds.back() != sorted.back()) {
        bucket_bounds.push_back(sorted.back());
    }
}

double EquiDepthHistogram::estimate_selectivity(QueryOp op, double val) const {
    if (bucket_bounds.empty() || total_count == 0.0) return 0.5;

    if (op == QueryOp::GT) {
        if (val >= bucket_bounds.back()) return 0.0;
        if (val < bucket_bounds.front()) return 1.0;

        auto it = std::upper_bound(bucket_bounds.begin(), bucket_bounds.end(), val);
        size_t bucket_idx = std::distance(bucket_bounds.begin(), it) - 1;

        double num_remaining_buckets = static_cast<double>(bucket_bounds.size() - 1 - bucket_idx - 1);
        double b_min = bucket_bounds[bucket_idx];
        double b_max = bucket_bounds[bucket_idx + 1];
        double fraction = (b_max - val) / (b_max - b_min + 1e-6);

        double matching_units = num_remaining_buckets + fraction;
        return matching_units / static_cast<double>(bucket_bounds.size() - 1);
    }

    if (op == QueryOp::LT) {
        if (val <= bucket_bounds.front()) return 0.0;
        if (val > bucket_bounds.back()) return 1.0;

        auto it = std::lower_bound(bucket_bounds.begin(), bucket_bounds.end(), val);
        size_t bucket_idx = std::distance(bucket_bounds.begin(), it) - 1;

        double num_completed_buckets = static_cast<double>(bucket_idx);
        double b_min = bucket_bounds[bucket_idx];
        double b_max = bucket_bounds[bucket_idx + 1];
        double fraction = (val - b_min) / (b_max - b_min + 1e-6);

        double matching_units = num_completed_buckets + fraction;
        return matching_units / static_cast<double>(bucket_bounds.size() - 1);
    }

    return 0.5;
}

} // namespace FenrirDB
