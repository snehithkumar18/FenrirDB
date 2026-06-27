#include "json_parser.h"
#include "logger.h"
#include <cctype>
#include <sstream>
#include <algorithm>

namespace FenrirDB {

char JSONLexer::peek() const {
    if (cursor >= src.size()) return '\0';
    return src[cursor];
}

char JSONLexer::next() {
    if (cursor >= src.size()) return '\0';
    return src[cursor++];
}

void JSONLexer::skip_whitespace() {
    while (cursor < src.size() && std::isspace(static_cast<unsigned char>(src[cursor]))) {
        cursor++;
    }
}

JSONLexer::JSONLexer(const std::string& json_str) : src(json_str) {}

std::vector<JSONToken> JSONLexer::tokenize() {
    std::vector<JSONToken> tokens;
    while (cursor < src.size()) {
        skip_whitespace();
        if (cursor >= src.size()) break;

        char c = peek();
        if (c == '{') {
            tokens.emplace_back(JSONTokenType::CURLY_OPEN, "{");
            next();
        } else if (c == '}') {
            tokens.emplace_back(JSONTokenType::CURLY_CLOSE, "}");
            next();
        } else if (c == '[') {
            tokens.emplace_back(JSONTokenType::BRACKET_OPEN, "[");
            next();
        } else if (c == ']') {
            tokens.emplace_back(JSONTokenType::BRACKET_CLOSE, "]");
            next();
        } else if (c == ':') {
            tokens.emplace_back(JSONTokenType::COLON, ":");
            next();
        } else if (c == ',') {
            tokens.emplace_back(JSONTokenType::COMMA, ",");
            next();
        } else if (c == '"') {
            next(); // Consume opening quote
            std::string str;
            while (peek() != '"' && peek() != '\0') {
                if (peek() == '\\') {
                    next(); // Consume escape character
                    char esc = next();
                    if (esc == 'n') str.push_back('\n');
                    else if (esc == 't') str.push_back('\t');
                    else if (esc == 'r') str.push_back('\r');
                    else str.push_back(esc);
                } else {
                    str.push_back(next());
                }
            }
            if (peek() == '"') next();
            tokens.emplace_back(JSONTokenType::STRING, str);
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            std::string num;
            num.push_back(next());
            while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
                num.push_back(next());
            }
            tokens.emplace_back(JSONTokenType::NUMBER, num);
        } else if (std::isalpha(static_cast<unsigned char>(c))) {
            std::string kw;
            while (std::isalpha(static_cast<unsigned char>(peek()))) {
                kw.push_back(next());
            }
            if (kw == "true") tokens.emplace_back(JSONTokenType::TRUE_VAL, "true");
            else if (kw == "false") tokens.emplace_back(JSONTokenType::FALSE_VAL, "false");
            else if (kw == "null") tokens.emplace_back(JSONTokenType::NULL_VAL, "null");
            else tokens.emplace_back(JSONTokenType::ERROR, kw);
        } else {
            std::string err_char(1, c);
            tokens.emplace_back(JSONTokenType::ERROR, err_char);
            next();
        }
    }
    tokens.emplace_back(JSONTokenType::END_OF_FILE, "");
    return tokens;
}

// ======================================================================
// JSONParser Implementation
// ======================================================================

JSONParser::JSONParser(const std::vector<JSONToken>& tks) : tokens(tks) {}

JSONToken JSONParser::peek() const {
    if (cursor >= tokens.size()) return JSONToken(JSONTokenType::END_OF_FILE, "");
    return tokens[cursor];
}

JSONToken JSONParser::next() {
    if (cursor >= tokens.size()) return JSONToken(JSONTokenType::END_OF_FILE, "");
    return tokens[cursor++];
}

bool JSONParser::match(JSONTokenType type) {
    if (peek().type == type) {
        next();
        return true;
    }
    return false;
}

bool JSONParser::expect(JSONTokenType type, const std::string& err_msg) {
    if (peek().type == type) {
        next();
        return true;
    }
    Logger::get_instance().error("JSONParser", "Syntax Error: " + err_msg);
    return false;
}

Variant JSONParser::parse_value() {
    JSONToken t = peek();
    if (match(JSONTokenType::STRING)) {
        return Variant(t.text);
    } else if (match(JSONTokenType::NUMBER)) {
        return Variant(std::stoi(t.text));
    } else if (match(JSONTokenType::TRUE_VAL)) {
        return Variant(true);
    } else if (match(JSONTokenType::FALSE_VAL)) {
        return Variant(false);
    } else if (match(JSONTokenType::NULL_VAL)) {
        return Variant();
    } else if (t.type == JSONTokenType::CURLY_OPEN) {
        return parse_object();
    } else if (t.type == JSONTokenType::BRACKET_OPEN) {
        return parse_array();
    }
    Logger::get_instance().error("JSONParser", "Unexpected token: " + t.text);
    return Variant();
}

Variant JSONParser::parse_object() {
    expect(JSONTokenType::CURLY_OPEN, "Expected '{'");
    std::unordered_map<std::string, Variant> obj;

    if (!match(JSONTokenType::CURLY_CLOSE)) {
        do {
            JSONToken key_tok = next();
            if (key_tok.type != JSONTokenType::STRING) {
                Logger::get_instance().error("JSONParser", "Expected object field key string");
                return Variant();
            }

            expect(JSONTokenType::COLON, "Expected ':' after field key");
            Variant val = parse_value();
            obj[key_tok.text] = val;
        } while (match(JSONTokenType::COMMA));

        expect(JSONTokenType::CURLY_CLOSE, "Expected '}' at object end");
    }
    return Variant(obj);
}

Variant JSONParser::parse_array() {
    expect(JSONTokenType::BRACKET_OPEN, "Expected '['");
    std::vector<Variant> arr;

    if (!match(JSONTokenType::BRACKET_CLOSE)) {
        do {
            arr.push_back(parse_value());
        } while (match(JSONTokenType::COMMA));

        expect(JSONTokenType::BRACKET_CLOSE, "Expected ']' at array end");
    }
    return Variant(arr);
}

Variant JSONParser::parse() {
    return parse_value();
}

// ======================================================================
// JSONSerializer Implementation
// ======================================================================

std::string JSONSerializer::serialize(const Variant& var, bool pretty, int indent) {
    std::string spaces = pretty ? std::string(indent * 2, ' ') : "";
    std::string nl = pretty ? "\n" : "";
    std::string sp = pretty ? " " : "";

    if (var.type == VariantType::NIL) {
        return "null";
    } else if (var.type == VariantType::INT) {
        return std::to_string(var.get_int());
    } else if (var.type == VariantType::BOOL) {
        return var.get_bool() ? "true" : "false";
    } else if (var.type == VariantType::STRING) {
        return "\"" + var.get_string() + "\"";
    } else if (var.type == VariantType::MAP) {
        std::unordered_map<std::string, Variant> map = var.get_map();
        if (map.empty()) return "{}";

        std::string res = "{" + nl;
        size_t count = 0;
        for (const auto& pair : map) {
            res += spaces + (pretty ? "  " : "") + "\"" + pair.first + "\":" + sp + serialize(pair.second, pretty, indent + 1);
            if (++count < map.size()) res += ",";
            res += nl;
        }
        res += spaces + "}";
        return res;
    } else if (var.type == VariantType::ARRAY) {
        std::vector<Variant> arr = var.get_array();
        if (arr.empty()) return "[]";

        std::string res = "[" + nl;
        for (size_t i = 0; i < arr.size(); ++i) {
            res += spaces + (pretty ? "  " : "") + serialize(arr[i], pretty, indent + 1);
            if (i + 1 < arr.size()) res += ",";
            res += nl;
        }
        res += spaces + "]";
        return res;
    }
    return "null";
}

} // namespace FenrirDB
