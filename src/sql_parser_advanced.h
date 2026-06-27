#ifndef FENRIRDB_SQL_PARSER_ADVANCED_H
#define FENRIRDB_SQL_PARSER_ADVANCED_H

#include "sql_parser.h"
#include "query_planner.h"
#include <string>
#include <vector>

namespace FenrirDB {

struct WindowSpec {
    std::string partition_col;
    std::string order_col;
    std::string function_name;
};

struct SQLStatement {
    std::vector<std::string> select_cols;
    std::string table_name;
    QueryNode filter_clause;
    
    // Advanced features
    WindowSpec window_rule;
    bool has_window;
    
    std::vector<std::string> group_cols;
    std::string agg_col;
    bool has_agg;
    
    bool is_recursive_cte;
    std::string cte_name;
    std::string cte_anchor_query;
    std::string cte_recursive_query;
};

class SQLParserAdvanced {
private:
    std::string sql_query;
    size_t cursor;

    std::string next_token();
    void skip_whitespace();

public:
    explicit SQLParserAdvanced(const std::string& query);
    ~SQLParserAdvanced() = default;

    bool parse_statement(SQLStatement& stmt);
};

} // namespace FenrirDB

#endif // FENRIRDB_SQL_PARSER_ADVANCED_H
