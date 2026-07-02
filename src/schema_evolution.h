#ifndef FENRIRDB_SCHEMA_EVOLUTION_H
#define FENRIRDB_SCHEMA_EVOLUTION_H

#include "catalog.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FenrirDB {

enum class MigrationActionType : uint8_t {
    ADD_FIELD = 1,
    DROP_FIELD = 2,
    RENAME_FIELD = 3,
    CHANGE_TYPE = 4,
    ADD_INDEX = 5,
    DROP_INDEX = 6,
    SET_DEFAULT = 7,
    ADD_CONSTRAINT = 8,
    DROP_CONSTRAINTS = 9
};

struct MigrationAction {
    MigrationActionType type = MigrationActionType::ADD_FIELD;
    std::string table;
    std::string name;
    std::string new_name;
    FieldKind field_kind = FieldKind::ANY;
    Variant value;
    FieldConstraint constraint;
    IndexDefinition index;
};

struct MigrationStep {
    uint32_t from_version = 0;
    uint32_t to_version = 0;
    std::string table;
    std::vector<MigrationAction> actions;
};

struct MigrationReport {
    bool success = false;
    std::vector<ValidationIssue> issues;
    Catalog before;
    Catalog after;
    size_t documents_rewritten = 0;
    size_t indexes_rebuilt = 0;
};

class SchemaCompatibility {
public:
    static std::vector<ValidationIssue> check_backward_compatible(const TableSchema& before,
                                                                  const TableSchema& after);
    static std::vector<ValidationIssue> check_document_rewrite(const TableSchema& target,
                                                               const std::vector<Document>& docs);
};

class SchemaMigrator {
public:
    explicit SchemaMigrator(Catalog catalog);

    MigrationReport apply(const std::vector<MigrationStep>& steps,
                          const std::vector<Document>& sample_documents);
    const Catalog& current_catalog() const { return catalog; }

private:
    Catalog catalog;

    bool apply_step(const MigrationStep& step, std::vector<ValidationIssue>& issues,
                    size_t& indexes_rebuilt);
    static bool apply_action(TableSchema& schema, const MigrationAction& action,
                             std::vector<ValidationIssue>& issues, size_t& indexes_rebuilt);
    static Document rewrite_document(const TableSchema& schema, const MigrationStep& step,
                                     const Document& doc);
};

class MigrationParser {
public:
    bool parse_text(const std::string& text, std::vector<MigrationStep>& out) const;
    std::string emit_text(const std::vector<MigrationStep>& steps) const;

private:
    static bool parse_action(const std::string& line, MigrationAction& out);
    static FieldKind parse_kind(const std::string& text);
    static MigrationActionType parse_action_type(const std::string& text);
    static Variant parse_value(const std::string& text, FieldKind kind);
};

} // namespace FenrirDB

#endif // FENRIRDB_SCHEMA_EVOLUTION_H
