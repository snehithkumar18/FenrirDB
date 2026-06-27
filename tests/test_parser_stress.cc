#include "../src/sql_parser.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_sql_lexer_stress() {
    std::cout << "Running test_sql_lexer_stress..." << std::endl;

    std::string sql = "SELECT id, name, salary FROM employees WHERE age >= 30 AND dept == 'Engineering' LIMIT 5;";
    FenrirDB::SQLLexer lexer(sql);
    std::vector<FenrirDB::SQLToken> tokens = lexer.tokenize();

    assert(!tokens.empty());
    assert(tokens[0].type == FenrirDB::SQLTokenType::SELECT);
    assert(tokens[1].text == "id");
    assert(tokens[2].type == FenrirDB::SQLTokenType::IDENTIFIER); // name
    assert(tokens[4].text == "salary");
    assert(tokens[6].type == FenrirDB::SQLTokenType::FROM);
    assert(tokens[7].text == "employees");
    assert(tokens[8].type == FenrirDB::SQLTokenType::WHERE);
    assert(tokens[9].text == "age");
    assert(tokens[10].type == FenrirDB::SQLTokenType::OPERATOR);
    assert(tokens[10].text == ">=");

    std::cout << "test_sql_lexer_stress passed." << std::endl;
}

void test_sql_parser_stress() {
    std::cout << "Running test_sql_parser_stress..." << std::endl;

    std::string sql = "SELECT id, name FROM users WHERE id = 'user_10' LIMIT 1;";
    FenrirDB::SQLLexer lexer(sql);
    std::vector<FenrirDB::SQLToken> tokens = lexer.tokenize();

    FenrirDB::SQLParser parser(tokens);
    FenrirDB::SQLSelectStatement stmt = parser.parse_select();

    assert(stmt.table_name == "users");
    assert(stmt.select_fields.size() == 2);
    assert(stmt.select_fields[0] == "id");
    assert(stmt.select_fields[1] == "name");
    assert(stmt.where_field == "id");
    assert(stmt.where_op == FenrirDB::QueryOp::EQ);
    assert(stmt.where_value.get_string() == "user_10");
    assert(stmt.limit == 1);

    std::cout << "test_sql_parser_stress passed." << std::endl;
}

void test_invalid_sql_parsing() {
    std::cout << "Running test_invalid_sql_parsing..." << std::endl;

    // Syntax missing table name
    std::string sql = "SELECT id FROM;";
    FenrirDB::SQLLexer lexer(sql);
    std::vector<FenrirDB::SQLToken> tokens = lexer.tokenize();

    FenrirDB::SQLParser parser(tokens);
    try {
        FenrirDB::SQLSelectStatement stmt = parser.parse_select();
        // Should either fail to assert or return empty table name
        assert(stmt.table_name.empty());
    } catch (...) {
        // Exception catching is fine
    }

    std::cout << "test_invalid_sql_parsing passed." << std::endl;
}

int main() {
    test_sql_lexer_stress();
    test_sql_parser_stress();
    test_invalid_sql_parsing();
    std::cout << "All Parser Stress tests passed successfully!" << std::endl;
    return 0;
}
