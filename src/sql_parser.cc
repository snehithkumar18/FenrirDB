#include "sql_parser.h"
#include "logger.h"
#include <cctype>
#include <algorithm>

namespace FenrirDB {

char SQLLexer::peek() const {
    if (cursor >= src.size()) return '\0';
    return src[cursor];
}

char SQLLexer::next() {
    if (cursor >= src.size()) return '\0';
    return src[cursor++];
}

void SQLLexer::skip_whitespace() {
    while (cursor < src.size() && std::isspace(static_cast<unsigned char>(src[cursor]))) {
        cursor++;
    }
}

SQLLexer::SQLLexer(const std::string& query) : src(query) {}

std::vector<Token> SQLLexer::tokenize() {
    std::vector<Token> tokens;
    while (cursor < src.size()) {
        skip_whitespace();
        if (cursor >= src.size()) break;

        char c = peek();
        if (c == '=') {
            tokens.emplace_back(TokenType::OP_EQUAL, "=");
            next();
        } else if (c == '>') {
            tokens.emplace_back(TokenType::OP_GREATER, ">");
            next();
        } else if (c == '<') {
            tokens.emplace_back(TokenType::OP_LESS, "<");
            next();
        } else if (c == ',') {
            tokens.emplace_back(TokenType::COMMA, ",");
            next();
        } else if (c == ';') {
            tokens.emplace_back(TokenType::SEMICOLON, ";");
            next();
        } else if (c == '(') {
            tokens.emplace_back(TokenType::PAREN_LEFT, "(");
            next();
        } else if (c == ')') {
            tokens.emplace_back(TokenType::PAREN_RIGHT, ")");
            next();
        } else if (c == '\'' || c == '"') {
            char quote = next(); // Consume quote char
            std::string literal;
            while (peek() != quote && peek() != '\0') {
                literal.push_back(next());
            }
            if (peek() == quote) {
                next(); // Consume closing quote
            }
            tokens.emplace_back(TokenType::STRING_LITERAL, literal);
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num;
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                num.push_back(next());
            }
            tokens.emplace_back(TokenType::NUMBER, num);
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string ident;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
                ident.push_back(next());
            }

            // Keyword check (case-insensitive)
            std::string upper_ident = ident;
            std::transform(upper_ident.begin(), upper_ident.end(), upper_ident.begin(), ::toupper);

            if (upper_ident == "SELECT") {
                tokens.emplace_back(TokenType::KEYWORD_SELECT, ident);
            } else if (upper_ident == "INSERT") {
                tokens.emplace_back(TokenType::KEYWORD_INSERT, ident);
            } else if (upper_ident == "INTO") {
                tokens.emplace_back(TokenType::KEYWORD_INTO, ident);
            } else if (upper_ident == "VALUES") {
                tokens.emplace_back(TokenType::KEYWORD_VALUES, ident);
            } else if (upper_ident == "UPDATE") {
                tokens.emplace_back(TokenType::KEYWORD_UPDATE, ident);
            } else if (upper_ident == "SET") {
                tokens.emplace_back(TokenType::KEYWORD_SET, ident);
            } else if (upper_ident == "DELETE") {
                tokens.emplace_back(TokenType::KEYWORD_DELETE, ident);
            } else if (upper_ident == "FROM") {
                tokens.emplace_back(TokenType::KEYWORD_FROM, ident);
            } else if (upper_ident == "WHERE") {
                tokens.emplace_back(TokenType::KEYWORD_WHERE, ident);
            } else {
                tokens.emplace_back(TokenType::IDENTIFIER, ident);
            }
        } else {
            std::string err_char(1, c);
            tokens.emplace_back(TokenType::ERROR, err_char);
            next();
        }
    }
    tokens.emplace_back(TokenType::END_OF_FILE, "");
    return tokens;
}

// ======================================================================
// SQLParser Implementation
// ======================================================================

SQLParser::SQLParser(const std::vector<Token>& tks) : tokens(tks) {}

Token SQLParser::peek() const {
    if (cursor >= tokens.size()) return Token(TokenType::END_OF_FILE, "");
    return tokens[cursor];
}

Token SQLParser::next() {
    if (cursor >= tokens.size()) return Token(TokenType::END_OF_FILE, "");
    return tokens[cursor++];
}

bool SQLParser::match(TokenType type) {
    if (peek().type == type) {
        next();
        return true;
    }
    return false;
}

bool SQLParser::expect(TokenType type, const std::string& err_msg) {
    if (peek().type == type) {
        next();
        return true;
    }
    Logger::get_instance().error("SQLParser", "Syntax Error: " + err_msg);
    return false;
}

std::unique_ptr<SQLSelectStatement> SQLParser::parse_select() {
    auto stmt = std::make_unique<SQLSelectStatement>();

    // Parse field names
    do {
        Token t = next();
        if (t.type != TokenType::IDENTIFIER && t.type != TokenType::OP_EQUAL) {
            Logger::get_instance().error("SQLParser", "Expected identifier in select field list");
            return nullptr;
        }
        stmt->fields.push_back(t.text);
    } while (match(TokenType::COMMA));

    if (!expect(TokenType::KEYWORD_FROM, "Expected 'FROM' after field list")) {
        return nullptr;
    }

    Token table_tok = next();
    if (table_tok.type != TokenType::IDENTIFIER) {
        Logger::get_instance().error("SQLParser", "Expected table identifier");
        return nullptr;
    }
    stmt->table = table_tok.text;
    stmt->table_name = table_tok.text;

    // Parse WHERE clause
    if (match(TokenType::KEYWORD_WHERE)) {
        Token field_tok = next();
        if (field_tok.type != TokenType::IDENTIFIER) {
            Logger::get_instance().error("SQLParser", "Expected WHERE field name");
            return nullptr;
        }
        stmt->where_field = field_tok.text;

        Token op_tok = next();
        if (op_tok.type == TokenType::OP_EQUAL) stmt->where_op = QueryOp::EQ;
        else if (op_tok.type == TokenType::OP_GREATER) stmt->where_op = QueryOp::GT;
        else if (op_tok.type == TokenType::OP_LESS) stmt->where_op = QueryOp::LT;
        else {
            Logger::get_instance().error("SQLParser", "Expected operator in WHERE condition");
            return nullptr;
        }

        Token val_tok = next();
        if (val_tok.type == TokenType::NUMBER) {
            stmt->where_value = Variant(std::stoi(val_tok.text));
        } else if (val_tok.type == TokenType::STRING_LITERAL) {
            stmt->where_value = Variant(val_tok.text);
        } else {
            Logger::get_instance().error("SQLParser", "Expected value literal in WHERE condition");
            return nullptr;
        }
    }

    return stmt;
}

std::unique_ptr<SQLInsertStatement> SQLParser::parse_insert() {
    auto stmt = std::make_unique<SQLInsertStatement>();

    if (!expect(TokenType::KEYWORD_INTO, "Expected 'INTO' after 'INSERT'")) {
        return nullptr;
    }

    Token table_tok = next();
    if (table_tok.type != TokenType::IDENTIFIER) {
        Logger::get_instance().error("SQLParser", "Expected table name");
        return nullptr;
    }
    stmt->table = table_tok.text;

    // Key insert key
    Token key_tok = next();
    if (key_tok.type != TokenType::IDENTIFIER) {
         Logger::get_instance().error("SQLParser", "Expected insert key identifier");
         return nullptr;
    }
    stmt->key = key_tok.text;

    if (!expect(TokenType::PAREN_LEFT, "Expected '(' before field list")) {
        return nullptr;
    }

    do {
        Token f = next();
        if (f.type != TokenType::IDENTIFIER) {
            Logger::get_instance().error("SQLParser", "Expected field name");
            return nullptr;
        }
        stmt->fields.push_back(f.text);
    } while (match(TokenType::COMMA));

    if (!expect(TokenType::PAREN_RIGHT, "Expected ')' after field list")) {
        return nullptr;
    }

    if (!expect(TokenType::KEYWORD_VALUES, "Expected 'VALUES'")) {
        return nullptr;
    }

    if (!expect(TokenType::PAREN_LEFT, "Expected '(' before values list")) {
        return nullptr;
    }

    do {
        Token v = next();
        if (v.type == TokenType::NUMBER) {
            stmt->values.push_back(Variant(std::stoi(v.text)));
        } else if (v.type == TokenType::STRING_LITERAL) {
            stmt->values.push_back(Variant(v.text));
        } else {
            Logger::get_instance().error("SQLParser", "Expected value literal");
            return nullptr;
        }
    } while (match(TokenType::COMMA));

    if (!expect(TokenType::PAREN_RIGHT, "Expected ')' after values list")) {
        return nullptr;
    }

    return stmt;
}

std::unique_ptr<SQLStatement> SQLParser::parse() {
    if (match(TokenType::KEYWORD_SELECT)) {
        return parse_select();
    } else if (match(TokenType::KEYWORD_INSERT)) {
        return parse_insert();
    }
    Logger::get_instance().error("SQLParser", "Unsupported initial statement keyword");
    return nullptr;
}

} // namespace FenrirDB
