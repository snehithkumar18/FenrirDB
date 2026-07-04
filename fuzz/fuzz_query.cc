#include "../src/database.h"
#include "../src/sql_parser.h"
#include "../src/query_engine_compiler_partition_optimizer.h"
#include "../src/query_engine_compiler_partition_hash.h"
#include "../src/optimizer_cbo_stats_histogram.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <cstring>
#include <memory>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10) return 0;

    std::string query_str(reinterpret_cast<const char*>(data), size);

    // 1. Fuzz normal Database Query
    FenrirDB::Database db;
    if (db.open("fuzz_query.db") == FenrirDB::DBErrorCode::SUCCESS) {
        std::vector<FenrirDB::Document> results;
        db.query(query_str, results);
        db.close();
        std::remove("fuzz_query.db");
    }

    // 2. Exercise SQL parser and partition optimizer together.
    FenrirDB::SQLLexer lexer(query_str);
    std::vector<FenrirDB::Token> tokens = lexer.tokenize();
    FenrirDB::SQLParser parser(tokens);
    try {
        auto stmt = parser.parse();
        if (stmt && stmt->type == FenrirDB::StatementType::SELECT) {
            auto* select_stmt = static_cast<FenrirDB::SQLSelectStatement*>(stmt.get());
            if (!select_stmt->table_name.empty() && !select_stmt->where_field.empty()) {
                FenrirDB::QueryNode filter_node{select_stmt->where_field, select_stmt->where_op, select_stmt->where_value};

                // Route hash-partitioned tables through partition pruning.
                if (select_stmt->table_name.find("hash") != std::string::npos) {
                    FenrirDB::HashPartitionManager hpm(select_stmt->table_name, select_stmt->where_field, 4);
                    FenrirDB::PartitionOptimizer part_opt;
                    part_opt.prune_partitions_optimized(hpm, filter_node);
                }
            }
        }
    } catch (...) {
        // Ignore parser exceptions
    }

    // 3. Exercise CBO statistics histogram ingestion.
    if (size >= 16) {
        double val1, val2;
        std::memcpy(&val1, data, 8);
        std::memcpy(&val2, data + 8, 8);
        
        FenrirDB::EquiWidthHistogram histogram(0.0, 100.0, 10);
        histogram.add_value(val1);
        histogram.add_value(val2);
    }

    return 0;
}


    }
