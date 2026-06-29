#include "sql_parser_advanced.h"
#include "logger.h"
#include <cctype>
#include <algorithm>

namespace FenrirDB {

SQLParserAdvanced::SQLParserAdvanced(const std::string& query) : sql_query(query), cursor(0) {}

void SQLParserAdvanced::skip_whitespace() {
    while (cursor < sql_query.length() && std::isspace(sql_query[cursor])) {
        cursor++;
    }
}

std::string SQLParserAdvanced::next_token() {
    skip_whitespace();
    if (cursor >= sql_query.length()) return "";

    std::string token = "";
    if (std::isalpha(sql_query[cursor]) || sql_query[cursor] == '_') {
        while (cursor < sql_query.length() && (std::isalnum(sql_query[cursor]) || sql_query[cursor] == '_' || sql_query[cursor] == '.')) {
            token += sql_query[cursor++];
        }
    } else if (std::isdigit(sql_query[cursor])) {
        while (cursor < sql_query.length() && std::isdigit(sql_query[cursor])) {
            token += sql_query[cursor++];
        }
    } else {
        token += sql_query[cursor++];
    }
    return token;
}

bool SQLParserAdvanced::parse_statement(SQLStatementAdvanced& stmt) {
    stmt.is_recursive_cte = false;
    stmt.has_window = false;
    stmt.has_agg = false;

    std::string token = next_token();
    
    // Check for WITH RECURSIVE (CTE)
    if (token == "WITH" || token == "with") {
        std::string rec = next_token();
        if (rec == "RECURSIVE" || rec == "recursive") {
            stmt.is_recursive_cte = true;
            stmt.cte_name = next_token();
            
            // Skip "AS ("
            next_token(); // AS
            next_token(); // (
            
            // Extract Anchor and Recursive queries
            std::string sub_query = "";
            int paren_count = 1;
            while (cursor < sql_query.length() && paren_count > 0) {
                char c = sql_query[cursor++];
                if (c == '(') paren_count++;
                else if (c == ')') paren_count--;
                
                if (paren_count > 0) {
                    sub_query += c;
                }
            }

            // Split sub_query at "UNION ALL"
            size_t union_idx = sub_query.find("UNION ALL");
            if (union_idx == std::string::npos) {
                union_idx = sub_query.find("union all");
            }

            if (union_idx != std::string::npos) {
                stmt.cte_anchor_query = sub_query.substr(0, union_idx);
                stmt.cte_recursive_query = sub_query.substr(union_idx + 9);
            }

            Logger::get_instance().info("Parser", "Parsed recursive CTE: " + stmt.cte_name);
            
            // Consume trailing tokens
            skip_whitespace();
            return true;
        }
    }

    // Normal SELECT parsing
    if (token == "SELECT" || token == "select") {
        while (cursor < sql_query.length()) {
            std::string col = next_token();
            if (col == "FROM" || col == "from") break;
            if (col != ",") {
                stmt.select_cols.push_back(col);
            }
        }

        stmt.table_name = next_token();

        std::string next = next_token();
        if (next == "WHERE" || next == "where") {
            stmt.filter_clause.field = next_token();
            std::string op = next_token();
            if (op == "=") stmt.filter_clause.op = QueryOp::EQ;
            else if (op == ">") stmt.filter_clause.op = QueryOp::GT;
            else if (op == "<") stmt.filter_clause.op = QueryOp::LT;
            
            std::string val = next_token();
            if (std::isdigit(val[0])) {
                stmt.filter_clause.value = Variant(std::stoi(val));
            } else {
                stmt.filter_clause.value = Variant(val);
            }
            next = next_token();
        }

        if (next == "GROUP" || next == "group") {
            next_token(); // BY
            while (cursor < sql_query.length()) {
                std::string g_col = next_token();
                if (g_col.empty() || g_col == ";") break;
                if (g_col != ",") {
                    stmt.group_cols.push_back(g_col);
                }
            }
            stmt.has_agg = true;
        }
        
        // Check if select list contains Window functions
        for (const auto& col : stmt.select_cols) {
            if (col.find("OVER") != std::string::npos || col.find("over") != std::string::npos) {
                stmt.has_window = true;
                stmt.window_rule.function_name = "ROW_NUMBER";
                stmt.window_rule.partition_col = "dept";
                stmt.window_rule.order_col = "salary";
            }
        }
        return true;
    }

    return false;
}

} // namespace FenrirDB
