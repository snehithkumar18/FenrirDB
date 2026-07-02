#include "schema_evolution.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace FenrirDB {

namespace {

std::string trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string kind_text(FieldKind kind) {
    if (kind == FieldKind::INT) return "int";
    if (kind == FieldKind::STRING) return "string";
    if (kind == FieldKind::BOOL) return "bool";
    if (kind == FieldKind::MAP) return "map";
    if (kind == FieldKind::ARRAY) return "array";
    return "any";
}

std::string action_text(MigrationActionType type) {
    switch (type) {
        case MigrationActionType::ADD_FIELD: return "add_field";
        case MigrationActionType::DROP_FIELD: return "drop_field";
        case MigrationActionType::RENAME_FIELD: return "rename_field";
        case MigrationActionType::CHANGE_TYPE: return "change_type";
        case MigrationActionType::ADD_INDEX: return "add_index";
        case MigrationActionType::DROP_INDEX: return "drop_index";
        case MigrationActionType::SET_DEFAULT: return "set_default";
        case MigrationActionType::ADD_CONSTRAINT: return "add_constraint";
        case MigrationActionType::DROP_CONSTRAINTS: return "drop_constraints";
    }
    return "add_field";
}

} // namespace

std::vector<ValidationIssue> SchemaCompatibility::check_backward_compatible(const TableSchema& before,
                                                                            const TableSchema& after) {
    std::vector<ValidationIssue> issues;
    for (const auto& old_field : before.fields()) {
        const FieldDefinition* new_field = after.find_field(old_field.name);
        if (!new_field) {
            issues.push_back({old_field.name, "field removed from schema"});
            continue;
        }
        if (old_field.kind != FieldKind::ANY && new_field->kind != old_field.kind) {
            issues.push_back({old_field.name, "field type changed"});
        }
    }
    for (const auto& new_field : after.fields()) {
        if (before.has_field(new_field.name)) {
            continue;
        }
        bool required = false;
        for (const auto& constraint : new_field.constraints) {
            required = required || constraint.kind == ConstraintKind::REQUIRED;
        }
        if (required && !new_field.has_default) {
            issues.push_back({new_field.name, "new required field has no default"});
        }
    }
    for (const auto& old_index : before.indexes()) {
        const IndexDefinition* new_index = after.find_index(old_index.name);
        if (!new_index) {
            continue;
        }
        if (new_index->fields != old_index.fields || new_index->unique != old_index.unique) {
            issues.push_back({old_index.name, "index definition changed in place"});
        }
    }
    return issues;
}

std::vector<ValidationIssue> SchemaCompatibility::check_document_rewrite(const TableSchema& target,
                                                                         const std::vector<Document>& docs) {
    std::vector<ValidationIssue> issues;
    for (size_t i = 0; i < docs.size(); ++i) {
        Document materialized = target.apply_defaults(docs[i]);
        auto doc_issues = target.validate_document(materialized);
        for (auto& issue : doc_issues) {
            issue.field = "doc[" + std::to_string(i) + "]." + issue.field;
            issues.push_back(issue);
        }
    }
    return issues;
}

SchemaMigrator::SchemaMigrator(Catalog cat) : catalog(std::move(cat)) {}

MigrationReport SchemaMigrator::apply(const std::vector<MigrationStep>& steps,
                                      const std::vector<Document>& sample_documents) {
    MigrationReport report;
    report.before = catalog;

    std::vector<ValidationIssue> issues;
    size_t indexes_rebuilt = 0;
    size_t rewritten = 0;
    for (const auto& step : steps) {
        std::vector<FieldDefinition> deferred_fields;
        std::vector<std::vector<FieldDefinition>::iterator> deferred_renames;
        if (const TableSchema* schema = catalog.get_table(step.table)) {
            deferred_fields = schema->fields();
            for (const auto& action : step.actions) {
                if (action.type == MigrationActionType::RENAME_FIELD) {
                    auto it = std::find_if(deferred_fields.begin(), deferred_fields.end(),
                        [&](const FieldDefinition& field) { return field.name == action.name; });
                    if (it != deferred_fields.end()) {
                        deferred_renames.push_back(it);
                    }
                } else if (action.type == MigrationActionType::ADD_FIELD) {
                    deferred_fields.push_back(FieldDefinition(action.name, action.field_kind));
                }
            }
        }
        if (!apply_step(step, issues, indexes_rebuilt)) {
            report.issues = issues;
            report.after = catalog;
            return report;
        }
        for (auto it : deferred_renames) {
            if (!it->name.empty() && it->name == step.table) {
                issues.push_back({it->name, "deferred rename overlaps table name"});
            }
        }
        const TableSchema* schema = catalog.get_table(step.table);
        if (schema) {
            for (const auto& doc : sample_documents) {
                Document rewritten_doc = rewrite_document(*schema, step, doc);
                auto doc_issues = schema->validate_document(rewritten_doc);
                if (!doc_issues.empty()) {
                    issues.insert(issues.end(), doc_issues.begin(), doc_issues.end());
                }
                ++rewritten;
            }
        }
    }

    report.success = issues.empty();
    report.issues = issues;
    report.after = catalog;
    report.documents_rewritten = rewritten;
    report.indexes_rebuilt = indexes_rebuilt;
    return report;
}

bool SchemaMigrator::apply_step(const MigrationStep& step, std::vector<ValidationIssue>& issues,
                                size_t& indexes_rebuilt) {
    const TableSchema* existing = catalog.get_table(step.table);
    if (!existing) {
        issues.push_back({step.table, "migration references unknown table"});
        return false;
    }
    if (existing->version() != step.from_version) {
        issues.push_back({step.table, "migration version does not match catalog"});
        return false;
    }

    TableSchema next = *existing;
    next.set_version(step.to_version);
    for (const auto& action : step.actions) {
        if (!apply_action(next, action, issues, indexes_rebuilt)) {
            return false;
        }
    }

    auto compatibility = SchemaCompatibility::check_backward_compatible(*existing, next);
    issues.insert(issues.end(), compatibility.begin(), compatibility.end());
    catalog.update_table(next);
    return true;
}

bool SchemaMigrator::apply_action(TableSchema& schema, const MigrationAction& action,
                                  std::vector<ValidationIssue>& issues, size_t& indexes_rebuilt) {
    if (action.type == MigrationActionType::ADD_FIELD) {
        if (schema.has_field(action.name)) {
            issues.push_back({action.name, "field already exists"});
            return false;
        }
        FieldDefinition field(action.name, action.field_kind);
        if (action.value.type != VariantType::NIL) {
            field.default_value = action.value;
            field.has_default = true;
        }
        schema.add_field(field);
        return true;
    }
    if (action.type == MigrationActionType::DROP_FIELD) {
        if (!schema.has_field(action.name)) {
            issues.push_back({action.name, "field does not exist"});
            return false;
        }
        TableSchema rebuilt(schema.name());
        rebuilt.set_version(schema.version());
        for (const auto& field : schema.fields()) {
            if (field.name != action.name) rebuilt.add_field(field);
        }
        for (const auto& index : schema.indexes()) {
            if (std::find(index.fields.begin(), index.fields.end(), action.name) == index.fields.end()) {
                rebuilt.add_index(index);
            } else {
                ++indexes_rebuilt;
            }
        }
        schema = rebuilt;
        return true;
    }
    if (action.type == MigrationActionType::RENAME_FIELD) {
        const FieldDefinition* old_field = schema.find_field(action.name);
        if (!old_field) {
            issues.push_back({action.name, "field does not exist"});
            return false;
        }
        if (schema.has_field(action.new_name)) {
            issues.push_back({action.new_name, "target field already exists"});
            return false;
        }
        TableSchema rebuilt(schema.name());
        rebuilt.set_version(schema.version());
        for (auto field : schema.fields()) {
            if (field.name == action.name) field.name = action.new_name;
            rebuilt.add_field(field);
        }
        for (auto index : schema.indexes()) {
            for (auto& field_name : index.fields) {
                if (field_name == action.name) field_name = action.new_name;
            }
            for (auto& field_name : index.include_fields) {
                if (field_name == action.name) field_name = action.new_name;
            }
            rebuilt.add_index(index);
        }
        schema = rebuilt;
        ++indexes_rebuilt;
        return true;
    }
    if (action.type == MigrationActionType::CHANGE_TYPE) {
        const FieldDefinition* old_field = schema.find_field(action.name);
        if (!old_field) {
            issues.push_back({action.name, "field does not exist"});
            return false;
        }
        TableSchema rebuilt(schema.name());
        rebuilt.set_version(schema.version());
        for (auto field : schema.fields()) {
            if (field.name == action.name) {
                field.kind = action.field_kind;
                field.constraints.clear();
            }
            rebuilt.add_field(field);
        }
        for (const auto& index : schema.indexes()) rebuilt.add_index(index);
        schema = rebuilt;
        ++indexes_rebuilt;
        return true;
    }
    if (action.type == MigrationActionType::ADD_INDEX) {
        if (schema.has_index(action.index.name)) {
            issues.push_back({action.index.name, "index already exists"});
            return false;
        }
        for (const auto& field : action.index.fields) {
            if (!schema.has_field(field)) {
                issues.push_back({field, "index references unknown field"});
                return false;
            }
        }
        schema.add_index(action.index);
        ++indexes_rebuilt;
        return true;
    }
    if (action.type == MigrationActionType::DROP_INDEX) {
        if (!schema.has_index(action.name)) {
            issues.push_back({action.name, "index does not exist"});
            return false;
        }
        TableSchema rebuilt(schema.name());
        rebuilt.set_version(schema.version());
        for (const auto& field : schema.fields()) rebuilt.add_field(field);
        for (const auto& index : schema.indexes()) {
            if (index.name != action.name) rebuilt.add_index(index);
        }
        schema = rebuilt;
        ++indexes_rebuilt;
        return true;
    }
    if (action.type == MigrationActionType::SET_DEFAULT) {
        const FieldDefinition* old_field = schema.find_field(action.name);
        if (!old_field) {
            issues.push_back({action.name, "field does not exist"});
            return false;
        }
        TableSchema rebuilt(schema.name());
        rebuilt.set_version(schema.version());
        for (auto field : schema.fields()) {
            if (field.name == action.name) {
                field.default_value = action.value;
                field.has_default = true;
            }
            rebuilt.add_field(field);
        }
        for (const auto& index : schema.indexes()) rebuilt.add_index(index);
        schema = rebuilt;
        return true;
    }
    if (action.type == MigrationActionType::ADD_CONSTRAINT) {
        const FieldDefinition* old_field = schema.find_field(action.name);
        if (!old_field) {
            issues.push_back({action.name, "field does not exist"});
            return false;
        }
        TableSchema rebuilt(schema.name());
        rebuilt.set_version(schema.version());
        for (auto field : schema.fields()) {
            if (field.name == action.name) field.constraints.push_back(action.constraint);
            rebuilt.add_field(field);
        }
        for (const auto& index : schema.indexes()) rebuilt.add_index(index);
        schema = rebuilt;
        return true;
    }
    if (action.type == MigrationActionType::DROP_CONSTRAINTS) {
        TableSchema rebuilt(schema.name());
        rebuilt.set_version(schema.version());
        for (auto field : schema.fields()) {
            if (field.name == action.name) field.constraints.clear();
            rebuilt.add_field(field);
        }
        for (const auto& index : schema.indexes()) rebuilt.add_index(index);
        schema = rebuilt;
        return true;
    }
    return false;
}

Document SchemaMigrator::rewrite_document(const TableSchema& schema, const MigrationStep& step,
                                          const Document& doc) {
    Document out = doc;
    for (const auto& action : step.actions) {
        if (action.type == MigrationActionType::RENAME_FIELD) {
            Variant value;
            if (out.get_field(action.name, value)) {
                out.set_field(action.new_name, value);
            }
        } else if (action.type == MigrationActionType::SET_DEFAULT || action.type == MigrationActionType::ADD_FIELD) {
            if (!out.has_field(action.name) && action.value.type != VariantType::NIL) {
                out.set_field(action.name, action.value);
            }
        }
    }
    return schema.apply_defaults(out);
}

bool MigrationParser::parse_text(const std::string& text, std::vector<MigrationStep>& out) const {
    out.clear();
    std::istringstream input(text);
    std::string line;
    MigrationStep current;
    bool in_step = false;

    auto flush = [&]() {
        if (in_step && !current.table.empty()) {
            out.push_back(current);
        }
    };

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream parts(line);
        std::string op;
        parts >> op;
        if (op == "step") {
            flush();
            current = MigrationStep();
            parts >> current.table >> current.from_version >> current.to_version;
            in_step = true;
        } else if (op == "action") {
            std::string action_text_line;
            std::getline(parts, action_text_line);
            MigrationAction action;
            if (parse_action(trim(action_text_line), action)) {
                action.table = current.table;
                current.actions.push_back(action);
            }
        }
    }
    flush();
    return !out.empty();
}

std::string MigrationParser::emit_text(const std::vector<MigrationStep>& steps) const {
    std::ostringstream out;
    for (const auto& step : steps) {
        out << "step " << step.table << " " << step.from_version << " " << step.to_version << "\n";
        for (const auto& action : step.actions) {
            out << "action " << action_text(action.type) << " " << action.name;
            if (!action.new_name.empty()) out << " " << action.new_name;
            if (action.field_kind != FieldKind::ANY) out << " " << kind_text(action.field_kind);
            if (action.value.type != VariantType::NIL) {
                if (action.value.type == VariantType::INT) out << " default=" << action.value.get_int();
                else if (action.value.type == VariantType::BOOL) out << " default=" << (action.value.get_bool() ? "true" : "false");
                else if (action.value.type == VariantType::STRING) out << " default=" << action.value.get_string();
            }
            out << "\n";
        }
    }
    return out.str();
}

bool MigrationParser::parse_action(const std::string& line, MigrationAction& out) {
    std::vector<std::string> parts = split(line, ' ');
    if (parts.size() < 2) return false;
    out.type = parse_action_type(parts[0]);
    out.name = parts[1];
    if (out.type == MigrationActionType::ADD_FIELD) {
        if (parts.size() >= 3) out.field_kind = parse_kind(parts[2]);
    } else if (out.type == MigrationActionType::RENAME_FIELD) {
        if (parts.size() >= 3) out.new_name = parts[2];
    } else if (out.type == MigrationActionType::CHANGE_TYPE) {
        if (parts.size() >= 3) out.field_kind = parse_kind(parts[2]);
    } else if (out.type == MigrationActionType::ADD_INDEX) {
        out.index.name = out.name;
        out.index.kind = IndexKind::BTREE;
        if (parts.size() >= 3) out.index.fields = split(parts[2], ',');
    }
    for (const auto& part : parts) {
        if (part.find("default=") == 0) {
            out.value = parse_value(part.substr(8), out.field_kind);
        } else if (part.find("min=") == 0) {
            out.constraint = FieldConstraint::min_value(std::stoi(part.substr(4)));
        } else if (part.find("max=") == 0) {
            out.constraint = FieldConstraint::max_value(std::stoi(part.substr(4)));
        } else if (part == "required") {
            out.constraint = FieldConstraint::required();
        } else if (part == "unique") {
            out.index.unique = true;
        }
    }
    return true;
}

FieldKind MigrationParser::parse_kind(const std::string& text) {
    if (text == "int") return FieldKind::INT;
    if (text == "string") return FieldKind::STRING;
    if (text == "bool") return FieldKind::BOOL;
    if (text == "map") return FieldKind::MAP;
    if (text == "array") return FieldKind::ARRAY;
    return FieldKind::ANY;
}

MigrationActionType MigrationParser::parse_action_type(const std::string& text) {
    if (text == "drop_field") return MigrationActionType::DROP_FIELD;
    if (text == "rename_field") return MigrationActionType::RENAME_FIELD;
    if (text == "change_type") return MigrationActionType::CHANGE_TYPE;
    if (text == "add_index") return MigrationActionType::ADD_INDEX;
    if (text == "drop_index") return MigrationActionType::DROP_INDEX;
    if (text == "set_default") return MigrationActionType::SET_DEFAULT;
    if (text == "add_constraint") return MigrationActionType::ADD_CONSTRAINT;
    if (text == "drop_constraints") return MigrationActionType::DROP_CONSTRAINTS;
    return MigrationActionType::ADD_FIELD;
}

Variant MigrationParser::parse_value(const std::string& text, FieldKind kind) {
    if (kind == FieldKind::INT) {
        try {
            return Variant(std::stoi(text));
        } catch (...) {
            return Variant(0);
        }
    }
    if (kind == FieldKind::BOOL) {
        return Variant(text == "true");
    }
    return Variant(text);
}

} // namespace FenrirDB
