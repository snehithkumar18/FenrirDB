#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_RANKING_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_RANKING_H

#include "query_planner.h"
#include <string>
#include <vector>

namespace FenrirDB {

enum class RankingFuncType {
    LAG,
    LEAD,
    FIRST_VALUE,
    LAST_VALUE
};

struct RankingSpec {
    std::string partition_col;
    std::string order_col;
    std::string target_col;
    RankingFuncType func;
    int offset = 1;
    Variant default_val;
};

class RankingWindowExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    RankingSpec spec;
    std::vector<Document> materialized_results;
    size_t cursor = 0;

    void compute_ranking_windows();

public:
    RankingWindowExecutor(std::unique_ptr<AbstractExecutor> ch, const RankingSpec& rs);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_RANKING_H
