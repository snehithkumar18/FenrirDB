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

struct Variant {
    VariantType type;
    void* val_ptr = nullptr;

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

    int get_int() const;       // Injected Bug 3 (Type Confusion)
    std::string get_string() const; // Injected Bug 3 (Type Confusion)
    bool get_bool() const;     // Injected Bug 3 (Type Confusion)
    std::unordered_map<std::string, Variant> get_map() const;
    std::vector<Variant> get_array() const;

    bool operator==(const Variant& other) const {
        if (type != other.type) return false;
        switch (type) {
            case VariantType::NIL:
                return true;
            case VariantType::INT:
                return get_int() == other.get_int();
            case VariantType::STRING:
                return get_string() == other.get_string();
            case VariantType::BOOL:
                return get_bool() == other.get_bool();
            default:
                return false;
        }
    }

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

    std::vector<uint8_t> serialize() const;
    static Document deserialize(const std::vector<uint8_t>& bytes);
};

enum class QueryOp {
    EQ,
    GT,
    LT
};

struct QueryNode {
    std::string field;
    QueryOp op;
    Variant value;
};

class QueryEvaluator {
public:
    static bool evaluate(const Document& doc, const QueryNode& query);
    static QueryNode parse_query_string(const std::string& query_str);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_H
