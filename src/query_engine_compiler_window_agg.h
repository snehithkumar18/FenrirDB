#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_AGG_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_AGG_H

#include "query_planner.h"
#include <string>
#include <vector>

namespace FenrirDB {

enum class RollingFuncType {
    STDDEV,
    VARIANCE,
    CUMULATIVE_SUM,
    PERCENTILE
};

struct RollingSpec {
    std::string partition_col;
    std::string order_col;
    std::string value_col;
    RollingFuncType func;
    double param = 0.0; // E.g., percentile threshold
};

class RollingWindowExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    RollingSpec spec;
    std::vector<Document> materialized_results;
    size_t cursor = 0;

    void compute_rolling_windows();

public:
    RollingWindowExecutor(std::unique_ptr<AbstractExecutor> ch, const RollingSpec& rs);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_AGG_H
