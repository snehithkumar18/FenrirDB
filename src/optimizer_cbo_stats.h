#ifndef FENRIRDB_OPTIMIZER_CBO_STATS_H
#define FENRIRDB_OPTIMIZER_CBO_STATS_H

#include "statistics.h"
#include <string>
#include <vector>

namespace FenrirDB {

class StatsEstimator {
private:
    const TableStatistics* table_stats;

public:
    explicit StatsEstimator(const TableStatistics* stats);
    ~StatsEstimator() = default;

    double estimate_predicate_selectivity(const QueryNode& node) const;
    double estimate_join_selectivity(const std::string& col1, const std::string& col2) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_OPTIMIZER_CBO_STATS_H
