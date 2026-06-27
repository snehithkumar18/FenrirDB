#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_RECURSIVE_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_RECURSIVE_H

#include "query_planner.h"
#include <string>
#include <vector>

namespace FenrirDB {

class RecursiveUnionExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> anchor_exec;
    std::unique_ptr<AbstractExecutor> recursive_exec;
    std::vector<Document> work_table;
    std::vector<Document> next_work_table;
    std::vector<Document> results_table;
    size_t cursor = 0;
    bool finished = false;

    void execute_recursive_loop();

public:
    RecursiveUnionExecutor(std::unique_ptr<AbstractExecutor> anchor, std::unique_ptr<AbstractExecutor> recursive);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;

    // Access to current iteration work table
    std::vector<Document>& get_work_table() { return work_table; }
    void set_next_work_table(const std::vector<Document>& next_w) { next_work_table = next_w; }
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_RECURSIVE_H
