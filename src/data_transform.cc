#include "data_transform.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace FenrirDB {

namespace {

void append_i32(std::vector<uint8_t>& out, int value) {
    uint8_t bytes[4];
    std::memcpy(bytes, &value, 4);
    out.insert(out.end(), bytes, bytes + 4);
}

bool read_i32(const std::vector<uint8_t>& bytes, size_t& offset, int& value) {
    if (offset + 4 > bytes.size()) return false;
    std::memcpy(&value, bytes.data() + offset, 4);
    offset += 4;
    return true;
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

bool read_u32(const std::vector<uint8_t>& bytes, size_t& offset, uint32_t& value) {
    if (offset + 4 > bytes.size()) return false;
    value = static_cast<uint32_t>(bytes[offset] | (bytes[offset + 1] << 8) |
                                  (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24));
    offset += 4;
    return true;
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string variant_to_key(const Variant& value) {
    if (value.type == VariantType::STRING) return value.get_string();
    if (value.type == VariantType::INT) return std::to_string(value.get_int());
    if (value.type == VariantType::BOOL) return value.get_bool() ? "true" : "false";
    return "";
}

} // namespace

EncodedColumn ColumnEncoder::encode(const ColumnBlock& block, ColumnEncoding encoding) const {
    if (encoding == ColumnEncoding::DICTIONARY) return encode_dictionary(block);
    if (encoding == ColumnEncoding::DELTA_INT) return encode_delta_int(block);
    if (encoding == ColumnEncoding::RUN_LENGTH) return encode_run_length(block);
    return encode_plain(block);
}

bool ColumnEncoder::decode(const EncodedColumn& encoded, ColumnBlock& out) const {
    if (encoded.encoding == ColumnEncoding::DICTIONARY) return decode_dictionary(encoded, out);
    if (encoded.encoding == ColumnEncoding::DELTA_INT) return decode_delta_int(encoded, out);
    if (encoded.encoding == ColumnEncoding::RUN_LENGTH) return decode_run_length(encoded, out);
    return decode_plain(encoded, out);
}

EncodedColumn ColumnEncoder::encode_plain(const ColumnBlock& block) const {
    EncodedColumn out;
    out.name = block.name;
    out.type = block.type;
    out.encoding = ColumnEncoding::PLAIN;
    out.value_count = block.values.size();
    for (const auto& value : block.values) {
        if (value.type == VariantType::INT) {
            append_i32(out.bytes, value.get_int());
        } else if (value.type == VariantType::BOOL) {
            out.bytes.push_back(value.get_bool() ? 1 : 0);
        } else if (value.type == VariantType::STRING) {
            std::string s = value.get_string();
            append_u32(out.bytes, static_cast<uint32_t>(s.size()));
            out.bytes.insert(out.bytes.end(), s.begin(), s.end());
        }
    }
    return out;
}

EncodedColumn ColumnEncoder::encode_dictionary(const ColumnBlock& block) const {
    EncodedColumn out;
    out.name = block.name;
    out.type = block.type;
    out.encoding = ColumnEncoding::DICTIONARY;
    out.value_count = block.values.size();
    std::map<std::string, uint32_t> ids;
    for (const auto& value : block.values) {
        std::string key = variant_to_key(value);
        auto it = ids.find(key);
        if (it == ids.end()) {
            uint32_t id = static_cast<uint32_t>(out.dictionary.size());
            ids[key] = id;
            out.dictionary.push_back(key);
            append_u32(out.bytes, id);
        } else {
            append_u32(out.bytes, it->second);
        }
    }
    return out;
}

EncodedColumn ColumnEncoder::encode_delta_int(const ColumnBlock& block) const {
    EncodedColumn out;
    out.name = block.name;
    out.type = block.type;
    out.encoding = ColumnEncoding::DELTA_INT;
    out.value_count = block.values.size();
    int previous = 0;
    bool first = true;
    for (const auto& value : block.values) {
        int current = value.type == VariantType::INT ? value.get_int() : 0;
        int stored = first ? current : current - previous;
        append_i32(out.bytes, stored);
        previous = current;
        first = false;
    }
    return out;
}

EncodedColumn ColumnEncoder::encode_run_length(const ColumnBlock& block) const {
    EncodedColumn out;
    out.name = block.name;
    out.type = block.type;
    out.encoding = ColumnEncoding::RUN_LENGTH;
    out.value_count = block.values.size();
    if (block.values.empty()) return out;

    Variant current = block.values[0];
    uint32_t count = 0;
    for (const auto& value : block.values) {
        if (value == current) {
            ++count;
            continue;
        }
        append_u32(out.bytes, count);
        if (current.type == VariantType::INT) append_i32(out.bytes, current.get_int());
        else if (current.type == VariantType::BOOL) out.bytes.push_back(current.get_bool() ? 1 : 0);
        else if (current.type == VariantType::STRING) {
            std::string s = current.get_string();
            append_u32(out.bytes, static_cast<uint32_t>(s.size()));
            out.bytes.insert(out.bytes.end(), s.begin(), s.end());
        }
        current = value;
        count = 1;
    }
    append_u32(out.bytes, count);
    if (current.type == VariantType::INT) append_i32(out.bytes, current.get_int());
    else if (current.type == VariantType::BOOL) out.bytes.push_back(current.get_bool() ? 1 : 0);
    else if (current.type == VariantType::STRING) {
        std::string s = current.get_string();
        append_u32(out.bytes, static_cast<uint32_t>(s.size()));
        out.bytes.insert(out.bytes.end(), s.begin(), s.end());
    }
    return out;
}

bool ColumnEncoder::decode_plain(const EncodedColumn& encoded, ColumnBlock& out) const {
    out.name = encoded.name;
    out.type = encoded.type;
    out.values.clear();
    size_t offset = 0;
    for (size_t i = 0; i < encoded.value_count; ++i) {
        if (encoded.type == VariantType::INT) {
            int value = 0;
            if (!read_i32(encoded.bytes, offset, value)) return false;
            out.values.push_back(Variant(value));
        } else if (encoded.type == VariantType::BOOL) {
            if (offset >= encoded.bytes.size()) return false;
            out.values.push_back(Variant(encoded.bytes[offset++] != 0));
        } else if (encoded.type == VariantType::STRING) {
            uint32_t len = 0;
            if (!read_u32(encoded.bytes, offset, len)) return false;
            if (offset + len > encoded.bytes.size()) return false;
            out.values.push_back(Variant(std::string(reinterpret_cast<const char*>(encoded.bytes.data() + offset), len)));
            offset += len;
        }
    }
    return true;
}

bool ColumnEncoder::decode_dictionary(const EncodedColumn& encoded, ColumnBlock& out) const {
    out.name = encoded.name;
    out.type = encoded.type;
    out.values.clear();
    size_t offset = 0;
    for (size_t i = 0; i < encoded.value_count; ++i) {
        uint32_t id = 0;
        if (!read_u32(encoded.bytes, offset, id)) return false;
        std::string text = encoded.dictionary[id];
        if (encoded.type == VariantType::INT) out.values.push_back(Variant(std::stoi(text)));
        else if (encoded.type == VariantType::BOOL) out.values.push_back(Variant(text == "true"));
        else out.values.push_back(Variant(text));
    }
    return true;
}

bool ColumnEncoder::decode_delta_int(const EncodedColumn& encoded, ColumnBlock& out) const {
    out.name = encoded.name;
    out.type = VariantType::INT;
    out.values.clear();
    size_t offset = 0;
    int previous = 0;
    for (size_t i = 0; i < encoded.value_count; ++i) {
        int delta = 0;
        if (!read_i32(encoded.bytes, offset, delta)) return false;
        int value = (i == 0) ? delta : previous + delta;
        out.values.push_back(Variant(value));
        previous = value;
    }
    return true;
}

bool ColumnEncoder::decode_run_length(const EncodedColumn& encoded, ColumnBlock& out) const {
    out.name = encoded.name;
    out.type = encoded.type;
    out.values.clear();
    size_t offset = 0;
    while (offset < encoded.bytes.size()) {
        uint32_t count = 0;
        if (!read_u32(encoded.bytes, offset, count)) return false;
        Variant value;
        if (encoded.type == VariantType::INT) {
            int v = 0;
            if (!read_i32(encoded.bytes, offset, v)) return false;
            value = Variant(v);
        } else if (encoded.type == VariantType::BOOL) {
            if (offset >= encoded.bytes.size()) return false;
            value = Variant(encoded.bytes[offset++] != 0);
        } else if (encoded.type == VariantType::STRING) {
            uint32_t len = 0;
            if (!read_u32(encoded.bytes, offset, len)) return false;
            if (offset + len > encoded.bytes.size()) return false;
            value = Variant(std::string(reinterpret_cast<const char*>(encoded.bytes.data() + offset), len));
            offset += len;
        }
        for (uint32_t i = 0; i < count; ++i) {
            out.values.push_back(value);
        }
    }
    return out.values.size() == encoded.value_count;
}

void ColumnarBatch::add_column(const ColumnBlock& block) {
    column_map[block.name] = block;
}

const ColumnBlock* ColumnarBatch::find_column(const std::string& name) const {
    auto it = column_map.find(name);
    if (it == column_map.end()) return nullptr;
    return &it->second;
}

std::vector<ColumnBlock> ColumnarBatch::columns() const {
    std::vector<ColumnBlock> out;
    out.reserve(column_map.size());
    for (const auto& pair : column_map) out.push_back(pair.second);
    return out;
}

size_t ColumnarBatch::row_count() const {
    size_t rows = 0;
    for (const auto& pair : column_map) {
        rows = std::max(rows, pair.second.values.size());
    }
    return rows;
}

std::vector<Document> ColumnarBatch::to_documents() const {
    size_t rows = row_count();
    std::vector<Document> docs(rows);
    for (const auto& pair : column_map) {
        const ColumnBlock& block = pair.second;
        for (size_t i = 0; i < block.values.size(); ++i) {
            docs[i].set_field(block.name, block.values[i]);
        }
    }
    return docs;
}

ColumnarBatch ColumnarBatch::from_documents(const std::vector<Document>& docs) {
    ColumnarBatch batch;
    std::map<std::string, ColumnBlock> blocks;
    for (const auto& doc : docs) {
        for (const auto& pair : doc.get_fields()) {
            ColumnBlock& block = blocks[pair.first];
            block.name = pair.first;
            block.type = pair.second.type;
            block.values.push_back(pair.second);
        }
    }
    for (const auto& pair : blocks) batch.add_column(pair.second);
    return batch;
}

ColumnarBatch TransformExecutor::execute(const ColumnarBatch& input, const TransformPlan& plan,
                                         std::vector<TransformIssue>& issues) const {
    std::vector<size_t> selected_rows;
    size_t rows = input.row_count();
    const ColumnBlock* filter = plan.has_filter ? input.find_column(plan.filter_column) : nullptr;
    if (plan.has_filter && !filter) {
        issues.push_back({plan.filter_column, "filter column is missing"});
        return ColumnarBatch();
    }
    for (size_t i = 0; i < rows; ++i) {
        bool selected = true;
        if (filter) {
            selected = i < filter->values.size() && value_matches(filter->values[i], plan.filter_op, plan.filter_value);
        }
        if (selected) {
            selected_rows.push_back(i);
            if (plan.limit != 0 && selected_rows.size() >= plan.limit) break;
        }
    }

    ColumnarBatch out;
    std::vector<std::string> projection = plan.projection.empty() ? std::vector<std::string>() : plan.projection;
    if (projection.empty()) {
        for (const auto& column : input.columns()) projection.push_back(column.name);
    }
    for (const auto& name : projection) {
        const ColumnBlock* source = input.find_column(name);
        if (!source) {
            issues.push_back({name, "projection column is missing"});
            continue;
        }
        ColumnBlock block;
        block.name = source->name;
        block.type = source->type;
        for (size_t row : selected_rows) {
            if (row < source->values.size()) block.values.push_back(source->values[row]);
        }
        out.add_column(block);
    }
    return out;
}

std::vector<EncodedColumn> TransformExecutor::encode_batch(const ColumnarBatch& batch,
                                                           ColumnEncoding preferred) const {
    ColumnEncoder encoder;
    std::vector<EncodedColumn> out;
    for (const auto& column : batch.columns()) {
        ColumnEncoding encoding = preferred;
        if (column.type != VariantType::INT && preferred == ColumnEncoding::DELTA_INT) {
            encoding = ColumnEncoding::DICTIONARY;
        }
        out.push_back(encoder.encode(column, encoding));
    }
    return out;
}

ColumnarBatch TransformExecutor::decode_batch(const std::vector<EncodedColumn>& columns,
                                              std::vector<TransformIssue>& issues) const {
    ColumnEncoder encoder;
    ColumnarBatch batch;
    for (const auto& encoded : columns) {
        ColumnBlock block;
        if (!encoder.decode(encoded, block)) {
            issues.push_back({encoded.name, "column decode failed"});
            continue;
        }
        batch.add_column(block);
    }
    return batch;
}

bool TransformExecutor::value_matches(const Variant& lhs, QueryOp op, const Variant& rhs) {
    if (lhs.type != rhs.type) return false;
    if (op == QueryOp::EQ) return lhs == rhs;
    if (op == QueryOp::NEQ) return lhs != rhs;
    if (lhs.type == VariantType::INT && op == QueryOp::GT) return lhs.get_int() > rhs.get_int();
    if (lhs.type == VariantType::INT && op == QueryOp::LT) return lhs.get_int() < rhs.get_int();
    return false;
}

bool TransformPlanParser::parse_text(const std::string& text, TransformPlan& out) const {
    out = TransformPlan();
    for (const auto& part : split(text, '|')) {
        size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        std::string key = part.substr(0, eq);
        std::string value = part.substr(eq + 1);
        if (key == "project") out.projection = split(value, ',');
        else if (key == "limit") out.limit = static_cast<size_t>(std::stoull(value));
        else if (key == "filter") {
            for (const std::string& op_text : {"!=", ">", "<", "="}) {
                size_t pos = value.find(op_text);
                if (pos != std::string::npos) {
                    out.filter_column = value.substr(0, pos);
                    out.filter_op = parse_op(op_text);
                    out.filter_value = parse_value(value.substr(pos + op_text.size()));
                    out.has_filter = true;
                    break;
                }
            }
        }
    }
    return !out.projection.empty() || out.has_filter || out.limit != 0;
}

std::string TransformPlanParser::emit_text(const TransformPlan& plan) const {
    std::ostringstream out;
    if (!plan.projection.empty()) {
        out << "project=";
        for (size_t i = 0; i < plan.projection.size(); ++i) {
            if (i) out << ",";
            out << plan.projection[i];
        }
    }
    if (plan.has_filter) {
        if (out.tellp() > 0) out << "|";
        out << "filter=" << plan.filter_column;
        if (plan.filter_op == QueryOp::NEQ) out << "!=";
        else if (plan.filter_op == QueryOp::GT) out << ">";
        else if (plan.filter_op == QueryOp::LT) out << "<";
        else out << "=";
        if (plan.filter_value.type == VariantType::INT) out << plan.filter_value.get_int();
        else if (plan.filter_value.type == VariantType::BOOL) out << (plan.filter_value.get_bool() ? "true" : "false");
        else if (plan.filter_value.type == VariantType::STRING) out << plan.filter_value.get_string();
    }
    if (plan.limit != 0) {
        if (out.tellp() > 0) out << "|";
        out << "limit=" << plan.limit;
    }
    return out.str();
}

Variant TransformPlanParser::parse_value(const std::string& text) {
    if (text == "true") return Variant(true);
    if (text == "false") return Variant(false);
    bool numeric = !text.empty();
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') {
            numeric = false;
            break;
        }
    }
    if (numeric) return Variant(std::stoi(text));
    return Variant(text);
}

QueryOp TransformPlanParser::parse_op(const std::string& text) {
    if (text == "!=") return QueryOp::NEQ;
    if (text == ">") return QueryOp::GT;
    if (text == "<") return QueryOp::LT;
    return QueryOp::EQ;
}

} // namespace FenrirDB
