#ifndef FENRIRDB_RUNTIME_EXPRESSION_H
#define FENRIRDB_RUNTIME_EXPRESSION_H

#include "query.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace FenrirDB {

class RuntimeRow {
public:
    void set(const std::string& key, const Variant& value);
    bool get(const std::string& key, Variant& value) const;
    std::vector<std::string> keys() const;

private:
    std::unordered_map<std::string, Variant> fields;
};

class RuntimeExpression {
public:
    enum class Kind {
        LITERAL,
        VARIABLE,
        CALL,
        BINARY
    };

    Kind kind = Kind::LITERAL;
    Variant literal;
    std::string name;
    char op = 0;
    std::vector<std::unique_ptr<RuntimeExpression>> args;
};

class RuntimeExpressionParser {
public:
    std::unique_ptr<RuntimeExpression> parse(const std::string& text);

private:
    std::string src;
    size_t cursor = 0;

    std::unique_ptr<RuntimeExpression> parse_expr(int min_prec);
    std::unique_ptr<RuntimeExpression> parse_primary();
    std::string parse_identifier();
    Variant parse_literal();
    void skip_ws();
    char peek() const;
    char next();
};

class RuntimeExpressionEngine {
public:
    RuntimeExpressionEngine();
    Variant evaluate(const RuntimeExpression& expr, const RuntimeRow& row) const;
    bool evaluate_bool(const std::string& text, const RuntimeRow& row) const;

private:
    using Function = Variant (*)(const std::vector<Variant>&);
    struct FunctionSpec {
        size_t min_args;
        size_t max_args;
        Function fn;
    };

    std::unordered_map<std::string, FunctionSpec> functions;

    Variant eval_call(const RuntimeExpression& expr, const RuntimeRow& row) const;
    Variant eval_binary(const RuntimeExpression& expr, const RuntimeRow& row) const;
    static std::string normalize_variable_name(const std::string& name);
};

} // namespace FenrirDB

#endif // FENRIRDB_RUNTIME_EXPRESSION_H
