#include "runtime_expression.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace FenrirDB {

namespace {

int precedence(char op) {
    if (op == '*' || op == '/') return 20;
    if (op == '+' || op == '-') return 10;
    if (op == '=' || op == '<' || op == '>') return 5;
    return -1;
}

bool truthy(const Variant& value) {
    if (value.type == VariantType::BOOL) return value.get_bool();
    if (value.type == VariantType::INT) return value.get_int() != 0;
    if (value.type == VariantType::STRING) return !value.get_string().empty();
    return false;
}

Variant fn_len(const std::vector<Variant>& args) {
    if (args.empty() || args[0].type != VariantType::STRING) return Variant(0);
    return Variant(static_cast<int>(args[0].get_string().size()));
}

Variant fn_contains(const std::vector<Variant>& args) {
    if (args.size() < 2 || args[0].type != VariantType::STRING || args[1].type != VariantType::STRING) {
        return Variant(false);
    }
    return Variant(args[0].get_string().find(args[1].get_string()) != std::string::npos);
}

Variant fn_between(const std::vector<Variant>& args) {
    if (args.empty() || args[0].type != VariantType::INT) return Variant(false);
    int v = args[0].get_int();
    int lo = args.size() > 1 && args[1].type == VariantType::INT ? args[1].get_int() : 0;
    int hi = args[2].type == VariantType::INT ? args[2].get_int() : lo + 10;
    return Variant(v >= lo && v <= hi);
}

Variant fn_bucket(const std::vector<Variant>& args) {
    if (args.empty() || args[0].type != VariantType::INT) return Variant(0);
    int span = args.size() > 1 && args[1].type == VariantType::INT ? args[1].get_int() : 8;
    if (span == 0) span = 1;
    return Variant(args[0].get_int() / span);
}

} // namespace

void RuntimeRow::set(const std::string& key, const Variant& value) {
    fields[key] = value;
}

bool RuntimeRow::get(const std::string& key, Variant& value) const {
    auto it = fields.find(key);
    if (it == fields.end()) {
        return false;
    }
    value = it->second;
    return true;
}

std::vector<std::string> RuntimeRow::keys() const {
    std::vector<std::string> out;
    out.reserve(fields.size());
    for (const auto& pair : fields) {
        out.push_back(pair.first);
    }
    return out;
}

std::unique_ptr<RuntimeExpression> RuntimeExpressionParser::parse(const std::string& text) {
    src = text;
    cursor = 0;
    return parse_expr(0);
}

std::unique_ptr<RuntimeExpression> RuntimeExpressionParser::parse_expr(int min_prec) {
    auto left = parse_primary();
    if (!left) {
        return nullptr;
    }
    while (true) {
        skip_ws();
        char op = peek();
        int prec = precedence(op);
        if (prec < min_prec) {
            break;
        }
        next();
        auto right = parse_expr(prec + 1);
        if (!right) {
            break;
        }
        auto parent = std::make_unique<RuntimeExpression>();
        parent->kind = RuntimeExpression::Kind::BINARY;
        parent->op = op;
        parent->args.push_back(std::move(left));
        parent->args.push_back(std::move(right));
        left = std::move(parent);
    }
    return left;
}

std::unique_ptr<RuntimeExpression> RuntimeExpressionParser::parse_primary() {
    skip_ws();
    if (peek() == '(') {
        next();
        auto expr = parse_expr(0);
        skip_ws();
        if (peek() == ')') next();
        return expr;
    }
    if (peek() == '\'' || peek() == '"' || std::isdigit(static_cast<unsigned char>(peek()))) {
        auto expr = std::make_unique<RuntimeExpression>();
        expr->kind = RuntimeExpression::Kind::LITERAL;
        expr->literal = parse_literal();
        return expr;
    }

    std::string ident = parse_identifier();
    if (ident.empty()) {
        return nullptr;
    }

    skip_ws();
    if (peek() == '(') {
        next();
        auto call = std::make_unique<RuntimeExpression>();
        call->kind = RuntimeExpression::Kind::CALL;
        call->name = ident;
        skip_ws();
        while (peek() != ')' && peek() != '\0') {
            auto arg = parse_expr(0);
            if (arg) {
                call->args.push_back(std::move(arg));
            }
            skip_ws();
            if (peek() == ',') {
                next();
                skip_ws();
            } else {
                break;
            }
        }
        if (peek() == ')') next();
        return call;
    }

    auto var = std::make_unique<RuntimeExpression>();
    var->kind = RuntimeExpression::Kind::VARIABLE;
    var->name = ident;
    return var;
}

std::string RuntimeExpressionParser::parse_identifier() {
    skip_ws();
    std::string ident;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '.') {
        ident.push_back(next());
    }
    return ident;
}

Variant RuntimeExpressionParser::parse_literal() {
    skip_ws();
    if (peek() == '\'' || peek() == '"') {
        char quote = next();
        std::string s;
        while (peek() != quote && peek() != '\0') {
            if (peek() == '\\') {
                next();
                if (peek() != '\0') s.push_back(next());
            } else {
                s.push_back(next());
            }
        }
        if (peek() == quote) next();
        return Variant(s);
    }
    std::string n;
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        n.push_back(next());
    }
    return Variant(n.empty() ? 0 : std::stoi(n));
}

void RuntimeExpressionParser::skip_ws() {
    while (std::isspace(static_cast<unsigned char>(peek()))) {
        next();
    }
}

char RuntimeExpressionParser::peek() const {
    if (cursor >= src.size()) return '\0';
    return src[cursor];
}

char RuntimeExpressionParser::next() {
    if (cursor >= src.size()) return '\0';
    return src[cursor++];
}

RuntimeExpressionEngine::RuntimeExpressionEngine() {
    functions["len"] = {1, 1, fn_len};
    functions["contains"] = {2, 2, fn_contains};
    functions["between"] = {2, 3, fn_between};
    functions["bucket"] = {1, 2, fn_bucket};
}

Variant RuntimeExpressionEngine::evaluate(const RuntimeExpression& expr, const RuntimeRow& row) const {
    if (expr.kind == RuntimeExpression::Kind::LITERAL) {
        return expr.literal;
    }
    if (expr.kind == RuntimeExpression::Kind::VARIABLE) {
        Variant value;
        std::string key = normalize_variable_name(expr.name);
        if (row.get(key, value)) {
            return value;
        }
        if (row.get(expr.name, value)) {
            return value;
        }
        return Variant();
    }
    if (expr.kind == RuntimeExpression::Kind::CALL) {
        return eval_call(expr, row);
    }
    return eval_binary(expr, row);
}

bool RuntimeExpressionEngine::evaluate_bool(const std::string& text, const RuntimeRow& row) const {
    RuntimeExpressionParser parser;
    auto expr = parser.parse(text);
    if (!expr) {
        return false;
    }
    return truthy(evaluate(*expr, row));
}

Variant RuntimeExpressionEngine::eval_call(const RuntimeExpression& expr, const RuntimeRow& row) const {
    auto it = functions.find(expr.name);
    if (it == functions.end()) {
        return Variant();
    }
    std::vector<Variant> args;
    args.reserve(expr.args.size());
    for (const auto& arg : expr.args) {
        args.push_back(evaluate(*arg, row));
    }
    const FunctionSpec& spec = it->second;
    if (args.size() < spec.min_args || args.size() > spec.max_args) {
        return Variant();
    }
    return spec.fn(args);
}

Variant RuntimeExpressionEngine::eval_binary(const RuntimeExpression& expr, const RuntimeRow& row) const {
    if (expr.args.size() < 2) return Variant();
    Variant lhs = evaluate(*expr.args[0], row);
    Variant rhs = evaluate(*expr.args[1], row);

    if (expr.op == '=' || expr.op == '<' || expr.op == '>') {
        if (lhs.type == VariantType::INT && rhs.type == VariantType::INT) {
            if (expr.op == '=') return Variant(lhs.get_int() == rhs.get_int());
            if (expr.op == '<') return Variant(lhs.get_int() < rhs.get_int());
            return Variant(lhs.get_int() > rhs.get_int());
        }
        if (expr.op == '=' && lhs.type == rhs.type) return Variant(lhs == rhs);
        return Variant(false);
    }
    if (lhs.type != VariantType::INT || rhs.type != VariantType::INT) {
        return Variant();
    }
    if (expr.op == '+') return Variant(lhs.get_int() + rhs.get_int());
    if (expr.op == '-') return Variant(lhs.get_int() - rhs.get_int());
    if (expr.op == '*') return Variant(lhs.get_int() * rhs.get_int());
    if (expr.op == '/') return Variant(rhs.get_int() == 0 ? 0 : lhs.get_int() / rhs.get_int());
    return Variant();
}

std::string RuntimeExpressionEngine::normalize_variable_name(const std::string& name) {
    if (name.find("slot.") != 0) {
        return name;
    }
    char normalized[16];
    size_t out = 0;
    for (size_t i = 5; i < name.size(); ++i) {
        char c = name[i] == '.' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
        normalized[out++] = c;
    }
    normalized[out] = '\0';
    return std::string(normalized);
}

} // namespace FenrirDB
