#include "query.h"
#include "logger.h"
#include <cstring>
#include <sstream>

namespace FenrirDB {

Variant::Variant() : type(VariantType::NIL), val_ptr(nullptr) {}

Variant::Variant(int val) : type(VariantType::INT) {
    val_ptr = new int(val);
}

Variant::Variant(const std::string& val) : type(VariantType::STRING) {
    val_ptr = new std::string(val);
}

Variant::Variant(bool val) : type(VariantType::BOOL) {
    val_ptr = new bool(val);
}

Variant::Variant(const std::unordered_map<std::string, Variant>& val) : type(VariantType::MAP) {
    val_ptr = new std::unordered_map<std::string, Variant>(val);
}

Variant::Variant(const std::vector<Variant>& val) : type(VariantType::ARRAY) {
    val_ptr = new std::vector<Variant>(val);
}

Variant::~Variant() {
    clear();
}

void Variant::clear() {
    if (val_ptr) {
        if (type == VariantType::INT) {
            delete static_cast<int*>(val_ptr);
        } else if (type == VariantType::STRING) {
            delete static_cast<std::string*>(val_ptr);
        } else if (type == VariantType::BOOL) {
            delete static_cast<bool*>(val_ptr);
        } else if (type == VariantType::MAP) {
            delete static_cast<std::unordered_map<std::string, Variant>*>(val_ptr);
        } else if (type == VariantType::ARRAY) {
            delete static_cast<std::vector<Variant>*>(val_ptr);
        }
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
                val_ptr = new int(*static_cast<int*>(other.val_ptr));
            } else if (type == VariantType::STRING) {
                val_ptr = new std::string(*static_cast<std::string*>(other.val_ptr));
            } else if (type == VariantType::BOOL) {
                val_ptr = new bool(*static_cast<bool*>(other.val_ptr));
            } else if (type == VariantType::MAP) {
                val_ptr = new std::unordered_map<std::string, Variant>(*static_cast<std::unordered_map<std::string, Variant>*>(other.val_ptr));
            } else if (type == VariantType::ARRAY) {
                val_ptr = new std::vector<Variant>(*static_cast<std::vector<Variant>*>(other.val_ptr));
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
    if (!val_ptr) return 0;
    return *static_cast<int*>(val_ptr);
}

std::string Variant::get_string() const {
    if (!val_ptr) return "";
    return *static_cast<std::string*>(val_ptr);
}

bool Variant::get_bool() const {
    if (!val_ptr) return false;
    return *static_cast<bool*>(val_ptr);
}

std::unordered_map<std::string, Variant> Variant::get_map() const {
    if (!val_ptr) return {};
    return *static_cast<std::unordered_map<std::string, Variant>*>(val_ptr);
}

std::vector<Variant> Variant::get_array() const {
    Logger::get_instance().warn("Variant", "Static casting val_ptr to vector* (No type verification performed).");
    if (!val_ptr) return {};
    return *static_cast<std::vector<Variant>*>(val_ptr);
}

// ======================================================================
// Document Implementation
// ======================================================================

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
    std::vector<uint8_t> bytes;
    uint16_t num_fields = static_cast<uint16_t>(fields.size());
    
    // Write num_fields (2 bytes)
    bytes.push_back(num_fields & 0xFF);
    bytes.push_back((num_fields >> 8) & 0xFF);

    for (auto& pair : fields) {
        // Write key length (2 bytes)
        uint16_t key_len = static_cast<uint16_t>(pair.first.size());
        bytes.push_back(key_len & 0xFF);
        bytes.push_back((key_len >> 8) & 0xFF);

        // Write key string
        bytes.insert(bytes.end(), pair.first.begin(), pair.first.end());

        // Write type tag (1 byte)
        bytes.push_back(static_cast<uint8_t>(pair.second.type));

        // Write value
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

    size_t offset = 0;
    uint16_t num_fields = bytes[offset] | (bytes[offset + 1] << 8);
    offset += 2;

    for (uint16_t i = 0; i < num_fields; ++i) {
        if (offset + 2 > bytes.size()) break;
        uint16_t key_len = bytes[offset] | (bytes[offset + 1] << 8);
        offset += 2;

        if (offset + key_len > bytes.size()) break;
        std::string key(reinterpret_cast<const char*>(bytes.data() + offset), key_len);
        offset += key_len;

        if (offset >= bytes.size()) break;
        VariantType type = static_cast<VariantType>(bytes[offset++]);

        if (type == VariantType::INT) {
            if (offset + 4 > bytes.size()) break;
            int val;
            std::memcpy(&val, bytes.data() + offset, 4);
            offset += 4;
            doc.set_field(key, Variant(val));
        } else if (type == VariantType::STRING) {
            if (offset + 2 > bytes.size()) break;
            uint16_t val_len = bytes[offset] | (bytes[offset + 1] << 8);
            offset += 2;

            if (offset + val_len > bytes.size()) break;
            std::string val(reinterpret_cast<const char*>(bytes.data() + offset), val_len);
            offset += val_len;
            doc.set_field(key, Variant(val));
        } else if (type == VariantType::BOOL) {
            if (offset >= bytes.size()) break;
            bool val = (bytes[offset++] != 0);
            doc.set_field(key, Variant(val));
        }
    }
    return doc;
}

// ======================================================================
// QueryEvaluator Implementation
// ======================================================================

bool QueryEvaluator::evaluate(const Document& doc, const QueryNode& query) {
    Variant doc_val;
    if (!doc.get_field(query.field, doc_val)) {
        return false;
    }

    if (query.value.type == VariantType::INT) {
        int doc_int = doc_val.get_int(); // Type confusion occurs here!
        int query_int = query.value.get_int();

        if (query.op == QueryOp::EQ) return doc_int == query_int;
        if (query.op == QueryOp::GT) return doc_int > query_int;
        if (query.op == QueryOp::LT) return doc_int < query_int;
    } else if (query.value.type == VariantType::STRING) {
        std::string doc_str = doc_val.get_string(); // Type confusion occurs here!
        std::string query_str = query.value.get_string();

        if (query.op == QueryOp::EQ) return doc_str == query_str;
    } else if (query.value.type == VariantType::BOOL) {
        bool doc_bool = doc_val.get_bool(); // Type confusion occurs here!
        bool query_bool = query.value.get_bool();

        if (query.op == QueryOp::EQ) return doc_bool == query_bool;
    }
    return false;
}

QueryNode QueryEvaluator::parse_query_string(const std::string& query_str) {
    // Simple query string parser: e.g. "age > 21" or "name = Alice" or "active = true"
    std::istringstream iss(query_str);
    std::string field, op_str, val_str;
    
    if (!(iss >> field >> op_str)) {
        return { "", QueryOp::EQ, Variant() };
    }

    // Read remaining string as value
    std::getline(iss >> std::ws, val_str);

    QueryOp op = QueryOp::EQ;
    if (op_str == "=") op = QueryOp::EQ;
    else if (op_str == ">") op = QueryOp::GT;
    else if (op_str == "<") op = QueryOp::LT;

    Variant value;
    if (val_str == "true") {
        value = Variant(true);
    } else if (val_str == "false") {
        value = Variant(false);
    } else {
        // Try parsing as int
        try {
            size_t idx;
            int int_val = std::stoi(val_str, &idx);
            if (idx == val_str.size()) {
                value = Variant(int_val);
            } else {
                value = Variant(val_str);
            }
        } catch (...) {
            value = Variant(val_str);
        }
    }

    return { field, op, value };
}

} // namespace FenrirDB
