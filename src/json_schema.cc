#include "json_schema.h"
#include "json_parser.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

JsonSchemaNode::JsonSchemaNode(SchemaType t)
    : type(t), minimum(0.0), maximum(0.0), has_min(false), has_max(false),
      min_length(0), max_length(0), has_min_len(false), has_max_len(false) {}

bool JsonSchemaNode::validate(const Variant& doc, std::vector<std::string>& errors) const {
    if (type == SchemaType::INTEGER) {
        if (doc.type != VariantType::INT) {
            errors.push_back("Expected Integer type.");
            return false;
        }
        int val = doc.get_int();
        if (has_min && val < minimum) {
            errors.push_back("Value " + std::to_string(val) + " is less than minimum: " + std::to_string(minimum));
            return false;
        }
        if (has_max && val > maximum) {
            errors.push_back("Value " + std::to_string(val) + " is greater than maximum: " + std::to_string(maximum));
            return false;
        }
    } else if (type == SchemaType::STRING) {
        if (doc.type != VariantType::STRING) {
            errors.push_back("Expected String type.");
            return false;
        }
        std::string val = doc.get_string();
        if (has_min_len && val.length() < min_length) {
            errors.push_back("String length " + std::to_string(val.length()) + " is less than minLength: " + std::to_string(min_length));
            return false;
        }
        if (has_max_len && val.length() > max_length) {
            errors.push_back("String length " + std::to_string(val.length()) + " is greater than maxLength: " + std::to_string(max_length));
            return false;
        }
    } else if (type == SchemaType::BOOLEAN) {
        if (doc.type != VariantType::BOOL) {
            errors.push_back("Expected Boolean type.");
            return false;
        }
    } else if (type == SchemaType::OBJECT) {
        if (doc.type != VariantType::MAP) {
            errors.push_back("Expected Object (Map) type.");
            return false;
        }
        auto fields = doc.get_map();
        // Check required fields
        for (const auto& req : required) {
            if (fields.find(req) == fields.end()) {
                errors.push_back("Missing required field: " + req);
                return false;
            }
        }
        // Validate properties
        for (const auto& prop : properties) {
            auto it = fields.find(prop.first);
            if (it != fields.end()) {
                if (!prop.second->validate(it->second, errors)) {
                    errors.push_back("Field '" + prop.first + "' failed schema validation.");
                    return false;
                }
            }
        }
    } else if (type == SchemaType::ARRAY) {
        // FenrirDB stores arrays as maps with index strings or simplified formats
        if (doc.type != VariantType::MAP) {
            errors.push_back("Expected Array (Map-backed) type.");
            return false;
        }
        if (items) {
            auto fields = doc.get_map();
            for (const auto& pair : fields) {
                if (!items->validate(pair.second, errors)) {
                    errors.push_back("Array element at index '" + pair.first + "' failed schema validation.");
                    return false;
                }
            }
        }
    }
    return true;
}

std::shared_ptr<JsonSchemaNode> JsonSchemaValidator::parse_schema_object(const Variant& schema_var) {
    if (schema_var.type != VariantType::MAP) return nullptr;
    auto map = schema_var.get_map();

    auto type_it = map.find("type");
    if (type_it == map.end() || type_it->second.type != VariantType::STRING) return nullptr;

    std::string type_str = type_it->second.get_string();
    SchemaType t = SchemaType::NIL;
    if (type_str == "object") t = SchemaType::OBJECT;
    else if (type_str == "array") t = SchemaType::ARRAY;
    else if (type_str == "string") t = SchemaType::STRING;
    else if (type_str == "integer") t = SchemaType::INTEGER;
    else if (type_str == "boolean") t = SchemaType::BOOLEAN;

    auto node = std::make_shared<JsonSchemaNode>(t);

    if (t == SchemaType::OBJECT) {
        // Parse required fields
        auto req_it = map.find("required");
        if (req_it != map.end() && req_it->second.type == VariantType::MAP) {
            // Arrays are serialized as index maps
            auto req_map = req_it->second.get_map();
            for (const auto& pair : req_map) {
                if (pair.second.type == VariantType::STRING) {
                    node->required.push_back(pair.second.get_string());
                }
            }
        }
        // Parse properties
        auto prop_it = map.find("properties");
        if (prop_it != map.end() && prop_it->second.type == VariantType::MAP) {
            auto prop_map = prop_it->second.get_map();
            for (const auto& pair : prop_map) {
                auto child_node = parse_schema_object(pair.second);
                if (child_node) {
                    node->properties[pair.first] = child_node;
                }
            }
        }
    } else if (t == SchemaType::ARRAY) {
        auto items_it = map.find("items");
        if (items_it != map.end()) {
            node->items = parse_schema_object(items_it->second);
        }
    } else if (t == SchemaType::INTEGER) {
        auto min_it = map.find("minimum");
        if (min_it != map.end() && min_it->second.type == VariantType::INT) {
            node->minimum = min_it->second.get_int();
            node->has_min = true;
        }
        auto max_it = map.find("maximum");
        if (max_it != map.end() && max_it->second.type == VariantType::INT) {
            node->maximum = max_it->second.get_int();
            node->has_max = true;
        }
    } else if (t == SchemaType::STRING) {
        auto min_len_it = map.find("minLength");
        if (min_len_it != map.end() && min_len_it->second.type == VariantType::INT) {
            node->min_length = min_len_it->second.get_int();
            node->has_min_len = true;
        }
        auto max_len_it = map.find("maxLength");
        if (max_len_it != map.end() && max_len_it->second.type == VariantType::INT) {
            node->max_length = max_len_it->second.get_int();
            node->has_max_len = true;
        }
    }

    return node;
}

bool JsonSchemaValidator::parse_schema(const std::string& schema_json) {
    JsonParser parser(schema_json);
    Variant v;
    if (!parser.parse(v)) {
        Logger::get_instance().error("Schema", "Failed to parse schema JSON string.");
        return false;
    }
    root_node = parse_schema_object(v);
    return root_node != nullptr;
}

bool JsonSchemaValidator::validate_document(const Document& doc, std::vector<std::string>& errors) const {
    if (!root_node) {
        errors.push_back("No schema parsed.");
        return false;
    }
    // Convert document to variant representation
    Variant doc_var;
    std::unordered_map<std::string, Variant> doc_map;
    for (const auto& pair : doc.get_fields()) {
        doc_map[pair.first] = pair.second;
    }
    doc_var.set_map(doc_map);
    return root_node->validate(doc_var, errors);
}

} // namespace FenrirDB
