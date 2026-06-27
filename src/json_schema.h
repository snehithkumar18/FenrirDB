#ifndef FENRIRDB_JSON_SCHEMA_H
#define FENRIRDB_JSON_SCHEMA_H

#include "query.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace FenrirDB {

enum class SchemaType {
    OBJECT,
    ARRAY,
    STRING,
    NUMBER,
    INTEGER,
    BOOLEAN,
    NIL
};

class JsonSchemaNode {
public:
    SchemaType type;
    std::vector<std::string> required;
    std::unordered_map<std::string, std::shared_ptr<JsonSchemaNode>> properties;
    std::shared_ptr<JsonSchemaNode> items; // For arrays

    // Numeric checks
    double minimum;
    double maximum;
    bool has_min;
    bool has_max;

    // String checks
    size_t min_length;
    size_t max_length;
    bool has_min_len;
    bool has_max_len;

    JsonSchemaNode(SchemaType t);
    bool validate(const Variant& doc, std::vector<std::string>& errors) const;
};

class JsonSchemaValidator {
private:
    std::shared_ptr<JsonSchemaNode> root_node;

    std::shared_ptr<JsonSchemaNode> parse_schema_object(const Variant& schema_var);

public:
    JsonSchemaValidator() = default;
    ~JsonSchemaValidator() = default;

    bool parse_schema(const std::string& schema_json);
    bool validate_document(const Document& doc, std::vector<std::string>& errors) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_JSON_SCHEMA_H
