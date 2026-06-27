#ifndef FENRIRDB_OPTIMIZER_CBO_STATS_HISTOGRAM_H
#define FENRIRDB_OPTIMIZER_CBO_STATS_HISTOGRAM_H

#include "statistics.h"
#include <vector>

namespace FenrirDB {

class EquiWidthHistogram {
private:
    double min_value;
    double max_value;
    size_t num_buckets;
    std::vector<double> bucket_counts;
    double total_count;

public:
    EquiWidthHistogram(double min_val, double max_val, size_t buckets);
    ~EquiWidthHistogram() = default;

    void add_value(double val);
    double estimate_selectivity(QueryOp op, double val) const;
};

class EquiDepthHistogram {
private:
    std::vector<double> bucket_bounds;
    double total_count;

public:
    EquiDepthHistogram() : total_count(0.0) {}
    ~EquiDepthHistogram() = default;

    void build(const std::vector<double>& values, size_t num_buckets);
    double estimate_selectivity(QueryOp op, double val) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_OPTIMIZER_CBO_STATS_HISTOGRAM_H
