#ifndef FENRIRDB_VECTORIZED_EXECUTOR_ADVANCED_H
#define FENRIRDB_VECTORIZED_EXECUTOR_ADVANCED_H

#include "vectorized_executor.h"
#include <unordered_map>
#include <string>
#include <vector>

namespace FenrirDB {

class VectorizedHashJoin : public VectorizedExecutor {
private:
    std::unique_ptr<VectorizedExecutor> left_child;
    std::unique_ptr<VectorizedExecutor> right_child;
    std::string left_col;
    std::string right_col;

    // Hash table mapping string keys to row descriptors from the left child
    std::unordered_multimap<std::string, std::unordered_map<std::string, Variant>> build_table;
    bool built;

    void build_hash_table();

public:
    VectorizedHashJoin(std::unique_ptr<VectorizedExecutor> left,
                       std::unique_ptr<VectorizedExecutor> right,
                       const std::string& l_col, const std::string& r_col);

    void init() override;
    bool next(VectorBatch& batch) override;
    void close() override;
};

struct AggregationState {
    int count;
    int sum;
    int min;
    int max;
};

class VectorizedAggregation : public VectorizedExecutor {
private:
    std::unique_ptr<VectorizedExecutor> child;
    std::vector<std::string> group_cols;
    std::string agg_col;
    
    std::unordered_map<std::string, AggregationState> agg_map;
    std::vector<std::pair<std::string, AggregationState>> finalized_results;
    size_t cursor;
    bool computed;

    void compute_aggregates();

public:
    VectorizedAggregation(std::unique_ptr<VectorizedExecutor> ch,
                          const std::vector<std::string>& g_cols,
                          const std::string& a_col);

    void init() override;
    bool next(VectorBatch& batch) override;
    void close() override;
};

} // namespace FenrirDB

#endif // FENRIRDB_VECTORIZED_EXECUTOR_ADVANCED_H
