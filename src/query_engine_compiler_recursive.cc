#include "query_engine_compiler_recursive.h"
#include "logger.h"

namespace FenrirDB {

RecursiveUnionExecutor::RecursiveUnionExecutor(std::unique_ptr<AbstractExecutor> anchor, std::unique_ptr<AbstractExecutor> recursive)
    : anchor_exec(std::move(anchor)), recursive_exec(std::move(recursive)) {}

void RecursiveUnionExecutor::execute_recursive_loop() {
    work_table.clear();
    next_work_table.clear();
    results_table.clear();

    // 1. Run anchor executor
    anchor_exec->init();
    Document doc;
    RecordID rid;
    while (anchor_exec->next(doc, rid)) {
        work_table.push_back(doc);
        results_table.push_back(doc);
    }
    anchor_exec->close();

    Logger::get_instance().info("RecursiveUnion", "Completed anchor phase. Found " + std::to_string(work_table.size()) + " rows.");

    // 2. Loop until no new rows are generated
    int iteration = 0;
    while (!work_table.empty() && iteration < 50) { // 50 is safety recursion limit
        next_work_table.clear();

        // Initialize recursive scanner
        recursive_exec->init();
        while (recursive_exec->next(doc, rid)) {
            next_work_table.push_back(doc);
        }
        recursive_exec->close();

        if (next_work_table.empty()) {
            break; // Terminate recursion
        }

        // Move next to work
        work_table = next_work_table;
        for (const auto& d : work_table) {
            results_table.push_back(d);
        }

        iteration++;
        Logger::get_instance().info("RecursiveUnion", "Iteration " + std::to_string(iteration) + " found " + std::to_string(work_table.size()) + " rows.");
    }
}

void RecursiveUnionExecutor::init() {
    execute_recursive_loop();
    cursor = 0;
    Logger::get_instance().info("RecursiveUnion", "RecursiveUnionExecutor initialized. Total results count=" + std::to_string(results_table.size()));
}

bool RecursiveUnionExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < results_table.size()) {
        doc = results_table[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void RecursiveUnionExecutor::close() {
    // Already closed anchor and recursive executors during loop
}

} // namespace FenrirDB
