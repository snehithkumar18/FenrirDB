#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_PIVOT_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_PIVOT_H

#include "query_planner.h"
#include <string>
#include <vector>

namespace FenrirDB {

struct PivotSpec {
    std::vector<std::string> group_cols;
    std::string pivot_col;
    std::string value_col;
    std::vector<std::string> pivot_values;
};

class PivotExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    PivotSpec spec;
    std::vector<Document> materialized_results;
    size_t cursor = 0;

    void compute_pivot_table();

public:
    PivotExecutor(std::unique_ptr<AbstractExecutor> ch, const PivotSpec& ps);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PIVOT_H
