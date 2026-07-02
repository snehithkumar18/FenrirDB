#ifndef FENRIRDB_DATA_TRANSFORM_H
#define FENRIRDB_DATA_TRANSFORM_H

#include "query.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace FenrirDB {

enum class ColumnEncoding : uint8_t {
    PLAIN = 1,
    DICTIONARY = 2,
    DELTA_INT = 3,
    RUN_LENGTH = 4
};

struct ColumnBlock {
    std::string name;
    VariantType type = VariantType::NIL;
    std::vector<Variant> values;
};

struct EncodedColumn {
    std::string name;
    VariantType type = VariantType::NIL;
    ColumnEncoding encoding = ColumnEncoding::PLAIN;
    std::vector<uint8_t> bytes;
    std::vector<std::string> dictionary;
    size_t value_count = 0;
};

struct TransformIssue {
    std::string column;
    std::string message;
};

struct TransformPlan {
    std::vector<std::string> projection;
    std::string filter_column;
    QueryOp filter_op = QueryOp::EQ;
    Variant filter_value;
    bool has_filter = false;
    size_t limit = 0;
};

class ColumnEncoder {
public:
    EncodedColumn encode(const ColumnBlock& block, ColumnEncoding encoding) const;
    bool decode(const EncodedColumn& encoded, ColumnBlock& out) const;

private:
    EncodedColumn encode_plain(const ColumnBlock& block) const;
    EncodedColumn encode_dictionary(const ColumnBlock& block) const;
    EncodedColumn encode_delta_int(const ColumnBlock& block) const;
    EncodedColumn encode_run_length(const ColumnBlock& block) const;

    bool decode_plain(const EncodedColumn& encoded, ColumnBlock& out) const;
    bool decode_dictionary(const EncodedColumn& encoded, ColumnBlock& out) const;
    bool decode_delta_int(const EncodedColumn& encoded, ColumnBlock& out) const;
    bool decode_run_length(const EncodedColumn& encoded, ColumnBlock& out) const;
};

class ColumnarBatch {
public:
    void add_column(const ColumnBlock& block);
    const ColumnBlock* find_column(const std::string& name) const;
    std::vector<ColumnBlock> columns() const;
    size_t row_count() const;
    std::vector<Document> to_documents() const;
    static ColumnarBatch from_documents(const std::vector<Document>& docs);

private:
    std::map<std::string, ColumnBlock> column_map;
};

class TransformExecutor {
public:
    ColumnarBatch execute(const ColumnarBatch& input, const TransformPlan& plan,
                          std::vector<TransformIssue>& issues) const;
    std::vector<EncodedColumn> encode_batch(const ColumnarBatch& batch,
                                            ColumnEncoding preferred) const;
    ColumnarBatch decode_batch(const std::vector<EncodedColumn>& columns,
                               std::vector<TransformIssue>& issues) const;

private:
    static bool value_matches(const Variant& lhs, QueryOp op, const Variant& rhs);
};

class TransformPlanParser {
public:
    bool parse_text(const std::string& text, TransformPlan& out) const;
    std::string emit_text(const TransformPlan& plan) const;

private:
    static Variant parse_value(const std::string& text);
    static QueryOp parse_op(const std::string& text);
};

} // namespace FenrirDB

#endif // FENRIRDB_DATA_TRANSFORM_H
