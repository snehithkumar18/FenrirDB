#ifndef FENRIRDB_QUERY_H
#define FENRIRDB_QUERY_H

#include <string>
#include <vector>
#include <unordered_map>
#include "errors.h"

namespace FenrirDB {

enum class VariantType : uint8_t {
    NIL = 0,
    INT = 1,
    STRING = 2,
    BOOL = 3,
    MAP = 4,
    ARRAY = 5
};

class VariantValue {
public:
    virtual ~VariantValue() = default;
    virtual VariantType get_type() const = 0;
};

class IntValue : public VariantValue {
public:
    int val;
    explicit IntValue(int v) : val(v) {}
    VariantType get_type() const override { return VariantType::INT; }
};

class StringValue : public VariantValue {
public:
    std::string val;
    explicit StringValue(const std::string& v) : val(v) {}
    VariantType get_type() const override { return VariantType::STRING; }
};

class BoolValue : public VariantValue {
public:
    bool val;
    explicit BoolValue(bool v) : val(v) {}
    VariantType get_type() const override { return VariantType::BOOL; }
};

struct Variant;

class MapValue : public VariantValue {
public:
    std::unordered_map<std::string, Variant> val;
    explicit MapValue(const std::unordered_map<std::string, Variant>& v) : val(v) {}
    VariantType get_type() const override { return VariantType::MAP; }
};

class ArrayValue : public VariantValue {
public:
    std::vector<Variant> val;
    explicit ArrayValue(const std::vector<Variant>& v) : val(v) {}
    VariantType get_type() const override { return VariantType::ARRAY; }
};

struct Variant {
    VariantType type;
    VariantValue* val_ptr = nullptr;

    Variant();
    explicit Variant(int val);
    explicit Variant(const std::string& val);
    explicit Variant(bool val);
    explicit Variant(const std::unordered_map<std::string, Variant>& val);
    explicit Variant(const std::vector<Variant>& val);
    ~Variant();

    Variant(const Variant& other);
    Variant& operator=(const Variant& other);
    Variant(Variant&& other) noexcept;
    Variant& operator=(Variant&& other) noexcept;

    std::unordered_map<std::string, Variant> get_map() const;
    std::vector<Variant> get_array() const;

    int get_int() const;
    std::string get_string() const;
    bool get_bool() const;

    bool operator==(const Variant& other) const;
    bool operator!=(const Variant& other) const {
        return !(*this == other);
    }

    void clear();
};

class Document {
private:
    std::unordered_map<std::string, Variant> fields;

public:
    Document() = default;
    ~Document() = default;

    void set_field(const std::string& key, const Variant& val);
    bool get_field(const std::string& key, Variant& val) const;
    bool has_field(const std::string& key) const;
    const std::unordered_map<std::string, Variant>& get_fields() const { return fields; }

    std::vector<uint8_t> serialize() const;
    static Document deserialize(const std::vector<uint8_t>& bytes);
};

enum class QueryOp {
    EQ,
    NEQ,
    GT,
    LT,
    AND,
    OR
};

struct QueryNode {
    std::string field;
    QueryOp op;
    Variant value;
    std::shared_ptr<QueryNode> left_child = nullptr;
    std::shared_ptr<QueryNode> right_child = nullptr;

    QueryNode() = default;
    QueryNode(const std::string& f, QueryOp o, const Variant& v)
        : field(f), op(o), value(v), left_child(nullptr), right_child(nullptr) {}
};

class QueryEvaluator {
public:
    static QueryNode parse_query_string(const std::string& query_str);
    static bool evaluate(const Document& doc, const QueryNode& query);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_H
