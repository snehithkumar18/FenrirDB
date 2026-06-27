#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_H

#include "query_planner.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace FenrirDB {

enum class WindowFuncType {
    ROW_NUMBER,
    RANK,
    DENSE_RANK,
    SUM,
    AVG
};

struct WindowSpec {
    std::string partition_by_col;
    std::string order_by_col;
    WindowFuncType func;
    std::string target_col;
};

class WindowExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    WindowSpec spec;
    std::vector<Document> materialized_results;
    size_t cursor = 0;

    void compute_window_functions();

public:
    WindowExecutor(std::unique_ptr<AbstractExecutor> ch, const WindowSpec& ws);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_WINDOW_H
