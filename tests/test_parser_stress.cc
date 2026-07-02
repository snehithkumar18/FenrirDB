#include "../src/sql_parser.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_sql_lexer_stress() {
    std::cout << "Running test_sql_lexer_stress..." << std::endl;

    std::string sql = "SELECT id, name, salary FROM employees WHERE age > 30;";
    FenrirDB::SQLLexer lexer(sql);
    std::vector<FenrirDB::Token> tokens = lexer.tokenize();

    assert(!tokens.empty());
    assert(tokens[0].type == FenrirDB::TokenType::KEYWORD_SELECT);
    assert(tokens[1].type == FenrirDB::TokenType::IDENTIFIER && tokens[1].text == "id");
    assert(tokens[2].type == FenrirDB::TokenType::COMMA);
    assert(tokens[3].type == FenrirDB::TokenType::IDENTIFIER && tokens[3].text == "name");
    assert(tokens[4].type == FenrirDB::TokenType::COMMA);
    assert(tokens[5].type == FenrirDB::TokenType::IDENTIFIER && tokens[5].text == "salary");
    assert(tokens[6].type == FenrirDB::TokenType::KEYWORD_FROM);
    assert(tokens[7].type == FenrirDB::TokenType::IDENTIFIER && tokens[7].text == "employees");
    assert(tokens[8].type == FenrirDB::TokenType::KEYWORD_WHERE);
    assert(tokens[9].type == FenrirDB::TokenType::IDENTIFIER && tokens[9].text == "age");
    assert(tokens[10].type == FenrirDB::TokenType::OP_GREATER);
    assert(tokens[11].type == FenrirDB::TokenType::NUMBER && tokens[11].text == "30");
    assert(tokens[12].type == FenrirDB::TokenType::SEMICOLON);

    std::cout << "test_sql_lexer_stress passed." << std::endl;
}

void test_sql_parser_stress() {
    std::cout << "Running test_sql_parser_stress..." << std::endl;

    std::string sql = "SELECT id, name FROM users WHERE id = 'user_10';";
    FenrirDB::SQLLexer lexer(sql);
    std::vector<FenrirDB::Token> tokens = lexer.tokenize();

    FenrirDB::SQLParser parser(tokens);
    auto stmt_ptr = parser.parse();
    assert(stmt_ptr != nullptr);
    assert(stmt_ptr->type == FenrirDB::StatementType::SELECT);

    auto* stmt = static_cast<FenrirDB::SQLSelectStatement*>(stmt_ptr.get());
    assert(stmt->table_name == "users");
    assert(stmt->fields.size() == 2);
    assert(stmt->fields[0] == "id");
    assert(stmt->fields[1] == "name");
    assert(stmt->where_field == "id");
    assert(stmt->where_op == FenrirDB::QueryOp::EQ);
    assert(stmt->where_value.get_string() == "user_10");

    std::cout << "test_sql_parser_stress passed." << std::endl;
}

void test_invalid_sql_parsing() {
    std::cout << "Running test_invalid_sql_parsing..." << std::endl;

    // Syntax missing table name
    std::string sql = "SELECT id FROM;";
    FenrirDB::SQLLexer lexer(sql);
    std::vector<FenrirDB::Token> tokens = lexer.tokenize();

    FenrirDB::SQLParser parser(tokens);
    auto stmt = parser.parse();
    assert(stmt == nullptr);

    std::cout << "test_invalid_sql_parsing passed." << std::endl;
}

int main() {
    test_sql_lexer_stress();
    test_sql_parser_stress();
    test_invalid_sql_parsing();
    std::cout << "All Parser Stress tests passed successfully!" << std::endl;
    return 0;
}
