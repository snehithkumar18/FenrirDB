#ifndef FENRIRDB_VECTORIZED_EXECUTOR_H
#define FENRIRDB_VECTORIZED_EXECUTOR_H

#include "query.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace FenrirDB {

constexpr size_t VECTOR_LIMIT = 1024;

struct VectorColumn {
    std::vector<Variant> data;
    bool has_nulls;
};

struct VectorBatch {
    std::unordered_map<std::string, VectorColumn> cols;
    std::vector<size_t> selection_vector; // Indices of valid rows
    size_t size;

    VectorBatch() : size(0) {}
    void clear();
    void add_column(const std::string& name, const std::vector<Variant>& data);
};

class VectorizedExecutor {
public:
    virtual ~VectorizedExecutor() = default;
    virtual void init() = 0;
    virtual bool next(VectorBatch& batch) = 0;
    virtual void close() = 0;
};

class VectorizedSeqScan : public VectorizedExecutor {
private:
    std::vector<Document> source_docs;
    size_t cursor;

public:
    explicit VectorizedSeqScan(const std::vector<Document>& docs);
    void init() override;
    bool next(VectorBatch& batch) override;
    void close() override;
};

class VectorizedFilter : public VectorizedExecutor {
private:
    std::unique_ptr<VectorizedExecutor> child;
    std::string filter_col;
    QueryOp filter_op;
    Variant filter_val;

public:
    VectorizedFilter(std::unique_ptr<VectorizedExecutor> ch, const std::string& col, QueryOp op, const Variant& val);
    void init() override;
    bool next(VectorBatch& batch) override;
    void close() override;
};

class VectorizedProjection : public VectorizedExecutor {
private:
    std::unique_ptr<VectorizedExecutor> child;
    std::vector<std::string> projection_cols;

public:
    VectorizedProjection(std::unique_ptr<VectorizedExecutor> ch, const std::vector<std::string>& cols);
    void init() override;
    bool next(VectorBatch& batch) override;
    void close() override;
};

} // namespace FenrirDB

#endif // FENRIRDB_VECTORIZED_EXECUTOR_H
