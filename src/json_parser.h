#ifndef FENRIRDB_JSON_PARSER_H
#define FENRIRDB_JSON_PARSER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "query.h"

namespace FenrirDB {

enum class JSONTokenType {
    CURLY_OPEN,
    CURLY_CLOSE,
    BRACKET_OPEN,
    BRACKET_CLOSE,
    COLON,
    COMMA,
    STRING,
    NUMBER,
    TRUE_VAL,
    FALSE_VAL,
    NULL_VAL,
    END_OF_FILE,
    ERROR
};

struct JSONToken {
    JSONTokenType type;
    std::string text;

    JSONToken(JSONTokenType t, const std::string& txt) : type(t), text(txt) {}
};

class JSONLexer {
private:
    std::string src;
    size_t cursor = 0;

    char peek() const;
    char next();
    void skip_whitespace();

public:
    explicit JSONLexer(const std::string& json_str);
    std::vector<JSONToken> tokenize();
};

class JSONParser {
private:
    std::vector<JSONToken> tokens;
    size_t cursor = 0;

    JSONToken peek() const;
    JSONToken next();
    bool match(JSONTokenType type);
    bool expect(JSONTokenType type, const std::string& err_msg);

    Variant parse_value();
    Variant parse_object();
    Variant parse_array();

public:
    explicit JSONParser(const std::vector<JSONToken>& tks);
    Variant parse();
};

class JSONSerializer {
public:
    static std::string serialize(const Variant& var, bool pretty = false, int indent = 0);
};

} // namespace FenrirDB

#endif // FENRIRDB_JSON_PARSER_H
