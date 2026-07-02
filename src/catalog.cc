#include "catalog.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <utility>

namespace FenrirDB {

namespace {

std::string trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(begin, end - begin);
}

std::string kind_to_string(FieldKind kind) {
    switch (kind) {
        case FieldKind::INT: return "int";
        case FieldKind::STRING: return "string";
        case FieldKind::BOOL: return "bool";
        case FieldKind::MAP: return "map";
        case FieldKind::ARRAY: return "array";
        default: return "any";
    }
}

std::string index_to_string(IndexKind kind) {
    switch (kind) {
        case IndexKind::HASH: return "hash";
        case IndexKind::COMPOSITE: return "composite";
        case IndexKind::COVERING: return "covering";
        default: return "btree";
    }
}

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

bool read_u16(const std::vector<uint8_t>& bytes, size_t& offset, uint16_t& value) {
    if (offset + 2 > bytes.size()) return false;
    value = static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    offset += 2;
    return true;
}

bool read_u32(const std::vector<uint8_t>& bytes, size_t& offset, uint32_t& value) {
    if (offset + 4 > bytes.size()) return false;
    value = static_cast<uint32_t>(bytes[offset] | (bytes[offset + 1] << 8) |
                                  (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24));
    offset += 4;
    return true;
}

void append_string(std::vector<uint8_t>& out, const std::string& value) {
    append_u16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool read_string(const std::vector<uint8_t>& bytes, size_t& offset, std::string& value) {
    uint16_t len = 0;
    if (!read_u16(bytes, offset, len)) return false;
    if (offset + len > bytes.size()) return false;
    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), len);
    offset += len;
    return true;
}

Variant constraint_variant(const FieldConstraint& constraint) {
    if (!constraint.text_value.empty()) return Variant(constraint.text_value);
    return Variant(constraint.int_value);
}

} // namespace

FieldConstraint FieldConstraint::required() {
    FieldConstraint c;
    c.kind = ConstraintKind::REQUIRED;
    return c;
}

FieldConstraint FieldConstraint::unique() {
    FieldConstraint c;
    c.kind = ConstraintKind::UNIQUE;
    return c;
}

FieldConstraint FieldConstraint::min_value(int value) {
    FieldConstraint c;
    c.kind = ConstraintKind::MIN_VALUE;
    c.int_value = value;
    return c;
}

FieldConstraint FieldConstraint::max_value(int value) {
    FieldConstraint c;
    c.kind = ConstraintKind::MAX_VALUE;
    c.int_value = value;
    return c;
}

FieldConstraint FieldConstraint::min_length(int value) {
    FieldConstraint c;
    c.kind = ConstraintKind::MIN_LENGTH;
    c.int_value = value;
    return c;
}

FieldConstraint FieldConstraint::max_length(int value) {
    FieldConstraint c;
    c.kind = ConstraintKind::MAX_LENGTH;
    c.int_value = value;
    return c;
}

FieldConstraint FieldConstraint::enum_value(const std::string& value) {
    FieldConstraint c;
    c.kind = ConstraintKind::ENUM_VALUE;
    c.text_value = value;
    return c;
}

FieldDefinition::FieldDefinition(std::string n, FieldKind k)
    : name(std::move(n)), kind(k) {}

TableSchema::TableSchema(std::string name) : table_name(std::move(name)) {}

void TableSchema::add_field(const FieldDefinition& field) {
    field_map[field.name] = field;
}

bool TableSchema::has_field(const std::string& field_name) const {
    return field_map.find(field_name) != field_map.end();
}

const FieldDefinition* TableSchema::find_field(const std::string& field_name) const {
    auto it = field_map.find(field_name);
    if (it == field_map.end()) return nullptr;
    return &it->second;
}

std::vector<FieldDefinition> TableSchema::fields() const {
    std::vector<FieldDefinition> out;
    out.reserve(field_map.size());
    for (const auto& pair : field_map) {
        out.push_back(pair.second);
    }
    return out;
}

void TableSchema::add_index(const IndexDefinition& index) {
    index_map[index.name] = index;
}

bool TableSchema::has_index(const std::string& index_name) const {
    return index_map.find(index_name) != index_map.end();
}

const IndexDefinition* TableSchema::find_index(const std::string& index_name) const {
    auto it = index_map.find(index_name);
    if (it == index_map.end()) return nullptr;
    return &it->second;
}

std::vector<IndexDefinition> TableSchema::indexes() const {
    std::vector<IndexDefinition> out;
    out.reserve(index_map.size());
    for (const auto& pair : index_map) {
        out.push_back(pair.second);
    }
    return out;
}

std::vector<ValidationIssue> TableSchema::validate_document(const Document& doc) const {
    std::vector<ValidationIssue> issues;
    for (const auto& pair : field_map) {
        const FieldDefinition& field = pair.second;
        Variant value;
        bool present = doc.get_field(field.name, value);

        for (const auto& constraint : field.constraints) {
            if (constraint.kind == ConstraintKind::REQUIRED && !present && !field.has_default) {
                issues.push_back({field.name, "required field is missing"});
            }
        }
        if (!present) {
            continue;
        }
        if (!type_matches(field.kind, value.type)) {
            issues.push_back({field.name, "field type does not match schema"});
            continue;
        }
        for (const auto& constraint : field.constraints) {
            if (constraint.kind == ConstraintKind::REQUIRED || constraint.kind == ConstraintKind::UNIQUE) {
                continue;
            }
            ValidationIssue issue;
            if (!validate_constraint(field, value, constraint, issue)) {
                issues.push_back(issue);
            }
        }
    }

    for (const auto& index_pair : index_map) {
        const IndexDefinition& index = index_pair.second;
        for (const std::string& field_name : index.fields) {
            if (!has_field(field_name)) {
                issues.push_back({field_name, "index references unknown field"});
            }
        }
        for (const std::string& field_name : index.include_fields) {
            if (!has_field(field_name)) {
                issues.push_back({field_name, "covering index includes unknown field"});
            }
        }
    }
    return issues;
}

Document TableSchema::apply_defaults(const Document& doc) const {
    Document out = doc;
    for (const auto& pair : field_map) {
        const FieldDefinition& field = pair.second;
        if (field.has_default && !out.has_field(field.name)) {
            out.set_field(field.name, field.default_value);
        }
    }
    return out;
}

std::string TableSchema::canonical_signature() const {
    std::ostringstream out;
    out << table_name << "#" << schema_version;
    for (const auto& pair : field_map) {
        const FieldDefinition& field = pair.second;
        out << "|f:" << field.name << ":" << kind_to_string(field.kind);
        for (const auto& constraint : field.constraints) {
            out << ":c" << static_cast<int>(constraint.kind);
            Variant cv = constraint_variant(constraint);
            if (cv.type == VariantType::INT) out << "=" << cv.get_int();
            if (cv.type == VariantType::STRING) out << "=" << cv.get_string();
        }
    }
    for (const auto& pair : index_map) {
        const IndexDefinition& index = pair.second;
        out << "|i:" << index.name << ":" << index_to_string(index.kind);
        out << ":" << (index.unique ? "u" : "n") << ":" << (index.sparse ? "s" : "d");
        for (const auto& field : index.fields) out << ":" << field;
        if (!index.include_fields.empty()) {
            out << ":include";
            for (const auto& field : index.include_fields) out << ":" << field;
        }
    }
    return out.str();
}

bool TableSchema::type_matches(FieldKind expected, VariantType actual) {
    if (expected == FieldKind::ANY) return true;
    if (expected == FieldKind::INT) return actual == VariantType::INT;
    if (expected == FieldKind::STRING) return actual == VariantType::STRING;
    if (expected == FieldKind::BOOL) return actual == VariantType::BOOL;
    if (expected == FieldKind::MAP) return actual == VariantType::MAP;
    if (expected == FieldKind::ARRAY) return actual == VariantType::ARRAY;
    return false;
}

bool TableSchema::validate_constraint(const FieldDefinition& field, const Variant& value,
                                      const FieldConstraint& constraint, ValidationIssue& issue) {
    issue.field = field.name;
    if (constraint.kind == ConstraintKind::MIN_VALUE && value.type == VariantType::INT) {
        if (value.get_int() < constraint.int_value) {
            issue.message = "integer value is below minimum";
            return false;
        }
    } else if (constraint.kind == ConstraintKind::MAX_VALUE && value.type == VariantType::INT) {
        if (value.get_int() > constraint.int_value) {
            issue.message = "integer value is above maximum";
            return false;
        }
    } else if (constraint.kind == ConstraintKind::MIN_LENGTH && value.type == VariantType::STRING) {
        if (static_cast<int>(value.get_string().size()) < constraint.int_value) {
            issue.message = "string value is shorter than minimum";
            return false;
        }
    } else if (constraint.kind == ConstraintKind::MAX_LENGTH && value.type == VariantType::STRING) {
        if (static_cast<int>(value.get_string().size()) > constraint.int_value) {
            issue.message = "string value exceeds maximum";
            return false;
        }
    } else if (constraint.kind == ConstraintKind::ENUM_VALUE && value.type == VariantType::STRING) {
        if (value.get_string() != constraint.text_value) {
            issue.message = "string value is outside enum set";
            return false;
        }
    }
    return true;
}

bool Catalog::create_table(const TableSchema& schema) {
    if (schema.name().empty() || has_table(schema.name())) {
        return false;
    }
    table_map[schema.name()] = schema;
    return true;
}

bool Catalog::drop_table(const std::string& table_name) {
    return table_map.erase(table_name) > 0;
}

bool Catalog::update_table(const TableSchema& schema) {
    if (schema.name().empty()) {
        return false;
    }
    table_map[schema.name()] = schema;
    return true;
}

bool Catalog::has_table(const std::string& table_name) const {
    return table_map.find(table_name) != table_map.end();
}

const TableSchema* Catalog::get_table(const std::string& table_name) const {
    auto it = table_map.find(table_name);
    if (it == table_map.end()) return nullptr;
    return &it->second;
}

std::vector<TableSchema> Catalog::tables() const {
    std::vector<TableSchema> out;
    out.reserve(table_map.size());
    for (const auto& pair : table_map) {
        out.push_back(pair.second);
    }
    return out;
}

std::vector<ValidationIssue> Catalog::validate_insert(const std::string& table_name, const Document& doc) const {
    const TableSchema* schema = get_table(table_name);
    if (!schema) {
        return {ValidationIssue{table_name, "table is not defined in catalog"}};
    }
    Document materialized = schema->apply_defaults(doc);
    return schema->validate_document(materialized);
}

Document Catalog::materialize_insert(const std::string& table_name, const Document& doc) const {
    const TableSchema* schema = get_table(table_name);
    if (!schema) {
        return doc;
    }
    return schema->apply_defaults(doc);
}

std::vector<uint8_t> Catalog::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'F', 'D', 'B', 'C'});
    append_u16(out, static_cast<uint16_t>(table_map.size()));
    for (const auto& table_pair : table_map) {
        const TableSchema& table = table_pair.second;
        append_string(out, table.name());
        append_u32(out, table.version());

        auto fields = table.fields();
        append_u16(out, static_cast<uint16_t>(fields.size()));
        for (const auto& field : fields) {
            append_string(out, field.name);
            out.push_back(static_cast<uint8_t>(field.kind));
            out.push_back(field.has_default ? 1 : 0);
            std::vector<uint8_t> default_bytes;
            if (field.has_default) {
                Document default_doc;
                default_doc.set_field("v", field.default_value);
                default_bytes = default_doc.serialize();
            }
            append_u16(out, static_cast<uint16_t>(default_bytes.size()));
            out.insert(out.end(), default_bytes.begin(), default_bytes.end());

            append_u16(out, static_cast<uint16_t>(field.constraints.size()));
            for (const auto& constraint : field.constraints) {
                out.push_back(static_cast<uint8_t>(constraint.kind));
                append_u32(out, static_cast<uint32_t>(constraint.int_value));
                append_string(out, constraint.text_value);
            }
        }

        auto indexes = table.indexes();
        append_u16(out, static_cast<uint16_t>(indexes.size()));
        for (const auto& index : indexes) {
            append_string(out, index.name);
            out.push_back(static_cast<uint8_t>(index.kind));
            out.push_back(index.unique ? 1 : 0);
            out.push_back(index.sparse ? 1 : 0);
            append_u16(out, static_cast<uint16_t>(index.fields.size()));
            for (const auto& field : index.fields) append_string(out, field);
            append_u16(out, static_cast<uint16_t>(index.include_fields.size()));
            for (const auto& field : index.include_fields) append_string(out, field);
        }
    }
    return out;
}

bool Catalog::deserialize(const std::vector<uint8_t>& bytes) {
    table_map.clear();
    if (bytes.size() < 6 || std::memcmp(bytes.data(), "FDBC", 4) != 0) {
        return false;
    }
    size_t offset = 4;
    uint16_t table_count = 0;
    if (!read_u16(bytes, offset, table_count)) return false;

    for (uint16_t t = 0; t < table_count; ++t) {
        std::string table_name;
        uint32_t version = 0;
        if (!read_string(bytes, offset, table_name)) return false;
        if (!read_u32(bytes, offset, version)) return false;
        TableSchema table(table_name);
        table.set_version(version);

        uint16_t field_count = 0;
        if (!read_u16(bytes, offset, field_count)) return false;
        for (uint16_t f = 0; f < field_count; ++f) {
            std::string field_name;
            if (!read_string(bytes, offset, field_name)) return false;
            if (offset + 2 > bytes.size()) return false;
            FieldDefinition field(field_name, static_cast<FieldKind>(bytes[offset++]));
            field.has_default = bytes[offset++] != 0;
            uint16_t default_len = 0;
            if (!read_u16(bytes, offset, default_len)) return false;
            if (offset + default_len > bytes.size()) return false;
            if (field.has_default) {
                std::vector<uint8_t> default_bytes(bytes.begin() + offset, bytes.begin() + offset + default_len);
                Document default_doc = Document::deserialize(default_bytes);
                Variant value;
                if (default_doc.get_field("v", value)) {
                    field.default_value = value;
                }
            }
            offset += default_len;

            uint16_t constraint_count = 0;
            if (!read_u16(bytes, offset, constraint_count)) return false;
            for (uint16_t c = 0; c < constraint_count; ++c) {
                if (offset + 5 > bytes.size()) return false;
                FieldConstraint constraint;
                constraint.kind = static_cast<ConstraintKind>(bytes[offset++]);
                uint32_t int_value = 0;
                if (!read_u32(bytes, offset, int_value)) return false;
                constraint.int_value = static_cast<int>(int_value);
                if (!read_string(bytes, offset, constraint.text_value)) return false;
                field.constraints.push_back(constraint);
            }
            table.add_field(field);
        }

        uint16_t index_count = 0;
        if (!read_u16(bytes, offset, index_count)) return false;
        for (uint16_t i = 0; i < index_count; ++i) {
            IndexDefinition index;
            if (!read_string(bytes, offset, index.name)) return false;
            if (offset + 3 > bytes.size()) return false;
            index.kind = static_cast<IndexKind>(bytes[offset++]);
            index.unique = bytes[offset++] != 0;
            index.sparse = bytes[offset++] != 0;
            uint16_t field_count_for_index = 0;
            if (!read_u16(bytes, offset, field_count_for_index)) return false;
            for (uint16_t f = 0; f < field_count_for_index; ++f) {
                std::string field;
                if (!read_string(bytes, offset, field)) return false;
                index.fields.push_back(field);
            }
            uint16_t include_count = 0;
            if (!read_u16(bytes, offset, include_count)) return false;
            for (uint16_t f = 0; f < include_count; ++f) {
                std::string field;
                if (!read_string(bytes, offset, field)) return false;
                index.include_fields.push_back(field);
            }
            table.add_index(index);
        }
        table_map[table.name()] = table;
    }
    return true;
}

CatalogDiff Catalog::diff(const Catalog& next) const {
    CatalogDiff diff;
    for (const auto& pair : table_map) {
        const TableSchema* other = next.get_table(pair.first);
        if (!other) {
            diff.removed_tables.push_back(pair.first);
        } else if (pair.second.canonical_signature() != other->canonical_signature()) {
            diff.changed_tables.push_back(pair.first);
        }
    }
    for (const auto& pair : next.table_map) {
        if (!has_table(pair.first)) {
            diff.added_tables.push_back(pair.first);
        }
    }
    return diff;
}

std::string Catalog::describe() const {
    std::ostringstream out;
    for (const auto& pair : table_map) {
        const TableSchema& table = pair.second;
        out << "table " << table.name() << " v" << table.version() << "\n";
        for (const auto& field : table.fields()) {
            out << "  field " << field.name << " " << kind_to_string(field.kind) << "\n";
        }
        for (const auto& index : table.indexes()) {
            out << "  index " << index.name << " " << index_to_string(index.kind);
            for (const auto& field : index.fields) out << " " << field;
            out << "\n";
        }
    }
    return out.str();
}

bool CatalogManifestParser::parse_text(const std::string& text, Catalog& out) const {
    out = Catalog();
    std::istringstream input(text);
    std::string line;
    TableSchema current;
    bool in_table = false;

    auto flush_table = [&]() {
        if (in_table && !current.name().empty()) {
            out.update_table(current);
        }
    };

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream parts(line);
        std::string op;
        parts >> op;
        if (op == "table") {
            flush_table();
            std::string name;
            parts >> name;
            current = TableSchema(name);
            in_table = true;
        } else if (op == "version") {
            uint32_t version = 1;
            parts >> version;
            current.set_version(version);
        } else if (op == "field") {
            std::string name, kind_text;
            parts >> name >> kind_text;
            FieldDefinition field(name, parse_field_kind(kind_text));
            std::string attr;
            while (parts >> attr) {
                if (attr == "required") field.constraints.push_back(FieldConstraint::required());
                else if (attr == "unique") field.constraints.push_back(FieldConstraint::unique());
                else if (attr.find("min=") == 0) field.constraints.push_back(FieldConstraint::min_value(std::stoi(attr.substr(4))));
                else if (attr.find("max=") == 0) field.constraints.push_back(FieldConstraint::max_value(std::stoi(attr.substr(4))));
                else if (attr.find("minlen=") == 0) field.constraints.push_back(FieldConstraint::min_length(std::stoi(attr.substr(7))));
                else if (attr.find("maxlen=") == 0) field.constraints.push_back(FieldConstraint::max_length(std::stoi(attr.substr(7))));
                else if (attr.find("enum=") == 0) field.constraints.push_back(FieldConstraint::enum_value(attr.substr(5)));
                else if (attr.find("default=") == 0) {
                    std::string value = attr.substr(8);
                    field.has_default = true;
                    if (field.kind == FieldKind::INT) field.default_value = Variant(std::stoi(value));
                    else if (field.kind == FieldKind::BOOL) field.default_value = Variant(value == "true");
                    else field.default_value = Variant(value);
                }
            }
            current.add_field(field);
        } else if (op == "index") {
            IndexDefinition index;
            std::string kind_text;
            parts >> index.name >> kind_text;
            index.kind = parse_index_kind(kind_text);
            std::string fields_text;
            parts >> fields_text;
            index.fields = split_csv(fields_text);
            std::string attr;
            while (parts >> attr) {
                if (attr == "unique") index.unique = true;
                else if (attr == "sparse") index.sparse = true;
                else if (attr.find("include=") == 0) index.include_fields = split_csv(attr.substr(8));
            }
            current.add_index(index);
        }
    }
    flush_table();
    return !out.tables().empty();
}

std::string CatalogManifestParser::emit_text(const Catalog& catalog) const {
    std::ostringstream out;
    for (const auto& table : catalog.tables()) {
        out << "table " << table.name() << "\n";
        out << "version " << table.version() << "\n";
        for (const auto& field : table.fields()) {
            out << "field " << field.name << " " << kind_to_string(field.kind);
            for (const auto& constraint : field.constraints) {
                if (constraint.kind == ConstraintKind::REQUIRED) out << " required";
                else if (constraint.kind == ConstraintKind::UNIQUE) out << " unique";
                else if (constraint.kind == ConstraintKind::MIN_VALUE) out << " min=" << constraint.int_value;
                else if (constraint.kind == ConstraintKind::MAX_VALUE) out << " max=" << constraint.int_value;
                else if (constraint.kind == ConstraintKind::MIN_LENGTH) out << " minlen=" << constraint.int_value;
                else if (constraint.kind == ConstraintKind::MAX_LENGTH) out << " maxlen=" << constraint.int_value;
                else if (constraint.kind == ConstraintKind::ENUM_VALUE) out << " enum=" << constraint.text_value;
            }
            out << "\n";
        }
        for (const auto& index : table.indexes()) {
            out << "index " << index.name << " " << index_to_string(index.kind) << " ";
            for (size_t i = 0; i < index.fields.size(); ++i) {
                if (i) out << ",";
                out << index.fields[i];
            }
            if (index.unique) out << " unique";
            if (index.sparse) out << " sparse";
            if (!index.include_fields.empty()) {
                out << " include=";
                for (size_t i = 0; i < index.include_fields.size(); ++i) {
                    if (i) out << ",";
                    out << index.include_fields[i];
                }
            }
            out << "\n";
        }
    }
    return out.str();
}

FieldKind CatalogManifestParser::parse_field_kind(const std::string& text) {
    if (text == "int") return FieldKind::INT;
    if (text == "string") return FieldKind::STRING;
    if (text == "bool") return FieldKind::BOOL;
    if (text == "map") return FieldKind::MAP;
    if (text == "array") return FieldKind::ARRAY;
    return FieldKind::ANY;
}

IndexKind CatalogManifestParser::parse_index_kind(const std::string& text) {
    if (text == "hash") return IndexKind::HASH;
    if (text == "composite") return IndexKind::COMPOSITE;
    if (text == "covering") return IndexKind::COVERING;
    return IndexKind::BTREE;
}

std::vector<std::string> CatalogManifestParser::split_csv(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

} // namespace FenrirDB
