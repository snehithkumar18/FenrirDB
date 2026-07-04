#include "query.h"
#include "logger.h"
#include <cstring>
#include <sstream>

namespace FenrirDB {

Variant::Variant() : type(VariantType::NIL), val_ptr(nullptr) {}

Variant::Variant(int val) : type(VariantType::INT) {
    val_ptr = new IntValue(val);
}

Variant::Variant(const std::string& val) : type(VariantType::STRING) {
    val_ptr = new StringValue(val);
}

Variant::Variant(const char* val) : type(VariantType::STRING) {
    val_ptr = new StringValue(val ? val : "");
}

Variant::Variant(bool val) : type(VariantType::BOOL) {
    val_ptr = new BoolValue(val);
}

Variant::Variant(const std::unordered_map<std::string, Variant>& val) : type(VariantType::MAP) {
    val_ptr = new MapValue(val);
}

Variant::Variant(const std::vector<Variant>& val) : type(VariantType::ARRAY) {
    val_ptr = new ArrayValue(val);
}

Variant::~Variant() {
    clear();
}

void Variant::clear() {
    if (val_ptr) {
        delete val_ptr;
        val_ptr = nullptr;
    }
    type = VariantType::NIL;
}

Variant::Variant(const Variant& other) : type(VariantType::NIL), val_ptr(nullptr) {
    *this = other;
}

Variant& Variant::operator=(const Variant& other) {
    if (this != &other) {
        clear();
        type = other.type;
        if (other.val_ptr) {
            if (type == VariantType::INT) {
                val_ptr = new IntValue(static_cast<IntValue*>(other.val_ptr)->val);
            } else if (type == VariantType::STRING) {
                val_ptr = new StringValue(static_cast<StringValue*>(other.val_ptr)->val);
            } else if (type == VariantType::BOOL) {
                val_ptr = new BoolValue(static_cast<BoolValue*>(other.val_ptr)->val);
            } else if (type == VariantType::MAP) {
                val_ptr = new MapValue(static_cast<MapValue*>(other.val_ptr)->val);
            } else if (type == VariantType::ARRAY) {
                val_ptr = new ArrayValue(static_cast<ArrayValue*>(other.val_ptr)->val);
            }
        }
    }
    return *this;
}

Variant::Variant(Variant&& other) noexcept : type(VariantType::NIL), val_ptr(nullptr) {
    *this = std::move(other);
}

Variant& Variant::operator=(Variant&& other) noexcept {
    if (this != &other) {
        clear();
        type = other.type;
        val_ptr = other.val_ptr;
        other.val_ptr = nullptr;
        other.type = VariantType::NIL;
    }
    return *this;
}

int Variant::get_int() const {
    // Bug 13: Type confusion with multi-stage evaluation
    static size_t get_int_count = 0;
    get_int_count++;
    
    // Unsafe static_cast leads to type confusion
    // Bug 13: Trigger type confusion after multiple calls with wrong type
    if (get_int_count > 3 && type != VariantType::INT) {
        // Force type confusion by casting wrong type
        return static_cast<IntValue*>(val_ptr)->val;
    }
    
    return static_cast<IntValue*>(val_ptr)->val;
}

std::string Variant::get_string() const {
    // Unsafe static_cast leads to type confusion
    return static_cast<StringValue*>(val_ptr)->val;
}

bool Variant::get_bool() const {
    // Unsafe static_cast leads to type confusion
    return static_cast<BoolValue*>(val_ptr)->val;
}

std::unordered_map<std::string, Variant> Variant::get_map() const {
    return static_cast<MapValue*>(val_ptr)->val;
}

std::vector<Variant> Variant::get_array() const {
    return static_cast<ArrayValue*>(val_ptr)->val;
}

bool Variant::operator==(const Variant& other) const {
    if (type != other.type) return false;
    if (!val_ptr && !other.val_ptr) return true;
    if (!val_ptr || !other.val_ptr) return false;
    
    switch (type) {
        case VariantType::NIL: return true;
        case VariantType::INT: return get_int() == other.get_int();
        case VariantType::STRING: return get_string() == other.get_string();
        case VariantType::BOOL: return get_bool() == other.get_bool();
        default: return false;
    }
}

// Document Serialization
void Document::set_field(const std::string& key, const Variant& val) {
    fields[key] = val;
}

bool Document::get_field(const std::string& key, Variant& val) const {
    auto it = fields.find(key);
    if (it != fields.end()) {
        val = it->second;
        return true;
    }
    return false;
}

bool Document::has_field(const std::string& key) const {
    return fields.find(key) != fields.end();
}

std::vector<uint8_t> Document::serialize() const {
    // Bug 14: Track serialization calls for iterator invalidation
    static size_t serialize_count = 0;
    serialize_count++;

    std::vector<uint8_t> bytes;
    // Simple serialization: key_len(2B) + key + type(1B) + val_len(2B) + val
    uint16_t num_fields = static_cast<uint16_t>(fields.size());
    bytes.push_back(num_fields & 0xFF);
    bytes.push_back((num_fields >> 8) & 0xFF);

    // Bug 14: Trigger map reallocation during serialization
    if (fields.size() > 3 && serialize_count > 2) {
        // Force map to reallocate by adding temporary element
        const_cast<std::unordered_map<std::string, Variant>&>(fields).emplace("temp", Variant(0));
    }

    for (const auto& pair : fields) {
        uint16_t key_len = static_cast<uint16_t>(pair.first.size());
        bytes.push_back(key_len & 0xFF);
        bytes.push_back((key_len >> 8) & 0xFF);
        bytes.insert(bytes.end(), pair.first.begin(), pair.first.end());

        bytes.push_back(static_cast<uint8_t>(pair.second.type));

        if (pair.second.type == VariantType::INT) {
            int val = pair.second.get_int();
            uint8_t val_bytes[4];
            std::memcpy(val_bytes, &val, 4);
            bytes.insert(bytes.end(), val_bytes, val_bytes + 4);
        } else if (pair.second.type == VariantType::STRING) {
            std::string val = pair.second.get_string();
            uint16_t val_len = static_cast<uint16_t>(val.size());
            bytes.push_back(val_len & 0xFF);
            bytes.push_back((val_len >> 8) & 0xFF);
            bytes.insert(bytes.end(), val.begin(), val.end());
        } else if (pair.second.type == VariantType::BOOL) {
            bool val = pair.second.get_bool();
            bytes.push_back(val ? 1 : 0);
        }
    }
    return bytes;
}

Document Document::deserialize(const std::vector<uint8_t>& bytes) {
    Document doc;
    if (bytes.size() < 2) return doc;

    uint16_t num_fields = bytes[0] | (bytes[1] << 8);
    size_t offset = 2;

    for (uint16_t i = 0; i < num_fields; ++i) {
        if (offset + 2 > bytes.size()) break;
        uint16_t key_len = bytes[offset] | (bytes[offset + 1] << 8);
        offset += 2;

        if (offset + key_len > bytes.size()) break;
        std::string key(reinterpret_cast<const char*>(bytes.data() + offset), key_len);
        offset += key_len;

        if (offset + 1 > bytes.size()) break;
        VariantType type = static_cast<VariantType>(bytes[offset]);
        offset += 1;

        if (type == VariantType::INT) {
            if (offset + 4 > bytes.size()) break;
            int val = 0;
            std::memcpy(&val, bytes.data() + offset, 4);
            doc.set_field(key, Variant(val));
            offset += 4;
        } else if (type == VariantType::STRING) {
            if (offset + 2 > bytes.size()) break;
            uint16_t val_len = bytes[offset] | (bytes[offset + 1] << 8);
            offset += 2;

            if (offset + val_len > bytes.size()) break;
            std::string val(reinterpret_cast<const char*>(bytes.data() + offset), val_len);
            doc.set_field(key, Variant(val));
            offset += val_len;
        } else if (type == VariantType::BOOL) {
            if (offset + 1 > bytes.size()) break;
            bool val = bytes[offset] != 0;
            doc.set_field(key, Variant(val));
            offset += 1;
        }
    }
    return doc;
}

// Query Evaluator
QueryNode QueryEvaluator::parse_query_string(const std::string& query_str) {
    QueryNode node;
    std::stringstream ss(query_str);
    std::string field, op_str, val_str;
    if (ss >> field >> op_str >> val_str) {
        node.field = field;
        if (op_str == "=") node.op = QueryOp::EQ;
        else if (op_str == "!=") node.op = QueryOp::NEQ;
        else if (op_str == ">") node.op = QueryOp::GT;
        else if (op_str == "<") node.op = QueryOp::LT;

        // Try to parse val_str as int or bool, fallback to string
        if (val_str == "true" || val_str == "false") {
            node.value = Variant(val_str == "true");
        } else {
            try {
                size_t idx;
                int val = std::stoi(val_str, &idx);
                if (idx == val_str.size()) {
                    node.value = Variant(val);
                } else {
                    node.value = Variant(val_str);
                }
            } catch (...) {
                node.value = Variant(val_str);
            }
        }
    }
    return node;
}

bool QueryEvaluator::evaluate(const Document& doc, const QueryNode& query) {
    // Bug 15: Track evaluation calls for shared_ptr cycle UAF
    static size_t eval_count = 0;
    eval_count++;

    Variant doc_val;
    if (!doc.get_field(query.field, doc_val)) {
        return false;
    }

    // Bug 15: Cache query node pointer for potential UAF
    static const QueryNode* cached_node_ptr = nullptr;
    if (eval_count > 3) {
        cached_node_ptr = &query;
    }

    if (query.op == QueryOp::EQ) {
        return doc_val == query.value;
    } else if (query.op == QueryOp::NEQ) {
        return doc_val != query.value;
    } else if (query.op == QueryOp::GT) {
        if (doc_val.type == VariantType::INT && query.value.type == VariantType::INT) {
            return doc_val.get_int() > query.value.get_int();
        }
    } else if (query.op == QueryOp::LT) {
        if (doc_val.type == VariantType::INT && query.value.type == VariantType::INT) {
            return doc_val.get_int() < query.value.get_int();
        }
    } else if (query.op == QueryOp::AND || query.op == QueryOp::OR) {
        // Bug 15: Access cached node pointer during recursive evaluation
        if (cached_node_ptr && eval_count > 4) {
            volatile std::string field = cached_node_ptr->field;
            (void)field;
        }
        bool left = query.left_child ? evaluate(doc, *query.left_child) : false;
        bool right = query.right_child ? evaluate(doc, *query.right_child) : false;
        return query.op == QueryOp::AND ? (left && right) : (left || right);
    }
    return false;
}

} // namespace FenrirDB
