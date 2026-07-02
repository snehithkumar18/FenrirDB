#ifndef FENRIRDB_CATALOG_H
#define FENRIRDB_CATALOG_H

#include "query.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace FenrirDB {

enum class FieldKind : uint8_t {
    ANY = 0,
    INT = 1,
    STRING = 2,
    BOOL = 3,
    MAP = 4,
    ARRAY = 5
};

enum class ConstraintKind : uint8_t {
    REQUIRED = 1,
    UNIQUE = 2,
    MIN_VALUE = 3,
    MAX_VALUE = 4,
    MIN_LENGTH = 5,
    MAX_LENGTH = 6,
    ENUM_VALUE = 7
};

struct FieldConstraint {
    ConstraintKind kind = ConstraintKind::REQUIRED;
    int int_value = 0;
    std::string text_value;

    static FieldConstraint required();
    static FieldConstraint unique();
    static FieldConstraint min_value(int value);
    static FieldConstraint max_value(int value);
    static FieldConstraint min_length(int value);
    static FieldConstraint max_length(int value);
    static FieldConstraint enum_value(const std::string& value);
};

struct FieldDefinition {
    std::string name;
    FieldKind kind = FieldKind::ANY;
    Variant default_value;
    bool has_default = false;
    std::vector<FieldConstraint> constraints;

    FieldDefinition() = default;
    FieldDefinition(std::string n, FieldKind k);
};

enum class IndexKind : uint8_t {
    HASH = 1,
    BTREE = 2,
    COMPOSITE = 3,
    COVERING = 4
};

struct IndexDefinition {
    std::string name;
    IndexKind kind = IndexKind::BTREE;
    std::vector<std::string> fields;
    std::vector<std::string> include_fields;
    bool unique = false;
    bool sparse = false;
};

struct ValidationIssue {
    std::string field;
    std::string message;
};

class TableSchema {
public:
    explicit TableSchema(std::string name = "");

    const std::string& name() const { return table_name; }
    uint32_t version() const { return schema_version; }
    void set_version(uint32_t version) { schema_version = version; }

    void add_field(const FieldDefinition& field);
    bool has_field(const std::string& field_name) const;
    const FieldDefinition* find_field(const std::string& field_name) const;
    std::vector<FieldDefinition> fields() const;

    void add_index(const IndexDefinition& index);
    bool has_index(const std::string& index_name) const;
    const IndexDefinition* find_index(const std::string& index_name) const;
    std::vector<IndexDefinition> indexes() const;

    std::vector<ValidationIssue> validate_document(const Document& doc) const;
    Document apply_defaults(const Document& doc) const;
    std::string canonical_signature() const;
    static bool type_matches(FieldKind expected, VariantType actual);

private:
    std::string table_name;
    uint32_t schema_version = 1;
    std::map<std::string, FieldDefinition> field_map;
    std::map<std::string, IndexDefinition> index_map;

    static bool validate_constraint(const FieldDefinition& field, const Variant& value,
                                    const FieldConstraint& constraint, ValidationIssue& issue);
};

struct CatalogDiff {
    std::vector<std::string> added_tables;
    std::vector<std::string> removed_tables;
    std::vector<std::string> changed_tables;
};

class Catalog {
public:
    bool create_table(const TableSchema& schema);
    bool drop_table(const std::string& table_name);
    bool update_table(const TableSchema& schema);
    bool has_table(const std::string& table_name) const;
    const TableSchema* get_table(const std::string& table_name) const;
    std::vector<TableSchema> tables() const;

    std::vector<ValidationIssue> validate_insert(const std::string& table_name, const Document& doc) const;
    Document materialize_insert(const std::string& table_name, const Document& doc) const;

    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& bytes);
    CatalogDiff diff(const Catalog& next) const;
    std::string describe() const;

private:
    std::map<std::string, TableSchema> table_map;
};

class CatalogManifestParser {
public:
    bool parse_text(const std::string& text, Catalog& out) const;
    std::string emit_text(const Catalog& catalog) const;

private:
    static FieldKind parse_field_kind(const std::string& text);
    static IndexKind parse_index_kind(const std::string& text);
    static std::vector<std::string> split_csv(const std::string& text);
};

} // namespace FenrirDB

#endif // FENRIRDB_CATALOG_H
