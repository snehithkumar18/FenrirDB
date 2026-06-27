#ifndef FENRIRDB_SQL_PARSER_H
#define FENRIRDB_SQL_PARSER_H

#include <string>
#include <vector>
#include <memory>
#include "query.h"

namespace FenrirDB {

enum class TokenType {
    KEYWORD_SELECT,
    KEYWORD_INSERT,
    KEYWORD_INTO,
    KEYWORD_VALUES,
    KEYWORD_UPDATE,
    KEYWORD_SET,
    KEYWORD_DELETE,
    KEYWORD_FROM,
    KEYWORD_WHERE,
    IDENTIFIER,
    NUMBER,
    STRING_LITERAL,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    COMMA,
    SEMICOLON,
    PAREN_LEFT,
    PAREN_RIGHT,
    END_OF_FILE,
    ERROR
};

struct Token {
    TokenType type;
    std::string text;

    Token(TokenType t, const std::string& txt) : type(t), text(txt) {}
};

class SQLLexer {
private:
    std::string src;
    size_t cursor = 0;

    char peek() const;
    char next();
    void skip_whitespace();

public:
    explicit SQLLexer(const std::string& query);
    std::vector<Token> tokenize();
};

enum class StatementType {
    SELECT,
    INSERT,
    UPDATE,
    DELETE
};

class SQLStatement {
public:
    StatementType type;
    explicit SQLStatement(StatementType t) : type(t) {}
    virtual ~SQLStatement() = default;
};

class SQLSelectStatement : public SQLStatement {
public:
    std::vector<std::string> fields;
    std::string table;
    std::string table_name;
    std::string where_field;
    QueryOp where_op;
    Variant where_value;

    std::string join_table;
    std::string join_on_outer;
    std::string join_on_inner;
    std::string agg_field;
    std::string group_field;
    std::string sort_field;
    int limit = 0;

    SQLSelectStatement() : SQLStatement(StatementType::SELECT), where_op(QueryOp::EQ), limit(0) {}
};

class SQLInsertStatement : public SQLStatement {
public:
    std::string table;
    std::string key;
    std::vector<std::string> fields;
    std::vector<Variant> values;

    SQLInsertStatement() : SQLStatement(StatementType::INSERT) {}
};

class SQLParser {
private:
    std::vector<Token> tokens;
    size_t cursor = 0;

    Token peek() const;
    Token next();
    bool match(TokenType type);
    bool expect(TokenType type, const std::string& err_msg);

    std::unique_ptr<SQLSelectStatement> parse_select();
    std::unique_ptr<SQLInsertStatement> parse_insert();

public:
    explicit SQLParser(const std::vector<Token>& tks);
    std::unique_ptr<SQLStatement> parse();
};

} // namespace FenrirDB

#endif // FENRIRDB_SQL_PARSER_H
