#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_PIVOT_UNPIVOT_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_PIVOT_UNPIVOT_H

#include "query_planner.h"
#include <string>
#include <vector>

namespace FenrirDB {

struct UnpivotSpec {
    std::vector<std::string> group_cols;
    std::string unpivot_val_col;
    std::string unpivot_name_col;
    std::vector<std::string> target_cols;
};

class UnpivotExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    UnpivotSpec spec;
    std::vector<Document> materialized_results;
    size_t cursor = 0;

    void compute_unpivot_table();

public:
    UnpivotExecutor(std::unique_ptr<AbstractExecutor> ch, const UnpivotSpec& us);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PIVOT_UNPIVOT_H
