#ifndef FENRIRDB_QUERY_PLANNER_H
#define FENRIRDB_QUERY_PLANNER_H

#include <memory>
#include <vector>
#include <string>
#include "database.h"
#include "sql_parser.h"

namespace FenrirDB {

// Volcano-style execution iterator interface
class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void init() = 0;
    virtual bool next(Document& doc, RecordID& rid) = 0;
    virtual void close() = 0;
};

class SeqScanExecutor : public AbstractExecutor {
private:
    DiskManager& disk_mgr;
    BufferPoolManager& cache_mgr;
    uint32_t cursor_page = 1;
    uint16_t cursor_slot = 0;
    uint32_t max_pages = 0;

public:
    SeqScanExecutor(DiskManager& dm, BufferPoolManager& cm);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class IndexScanExecutor : public AbstractExecutor {
private:
    BPlusTreeIndex& index;
    BufferPoolManager& cache_mgr;
    std::string key;
    bool fetched = false;

public:
    IndexScanExecutor(BPlusTreeIndex& idx, BufferPoolManager& cm, const std::string& k);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class FilterExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::string field;
    QueryOp op;
    Variant val;

public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& f, QueryOp o, const Variant& v);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class LimitExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    size_t limit;
    size_t count = 0;

public:
    LimitExecutor(std::unique_ptr<AbstractExecutor> ch, size_t lim);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class NestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> outer;
    std::unique_ptr<AbstractExecutor> inner;
    std::string outer_col;
    std::string inner_col;
    Document outer_doc;
    RecordID outer_rid;
    bool has_outer = false;

public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> out, std::unique_ptr<AbstractExecutor> in,
                           const std::string& out_c, const std::string& in_c);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class HashJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> outer;
    std::unique_ptr<AbstractExecutor> inner;
    std::string outer_col;
    std::string inner_col;
    std::unordered_map<std::string, std::vector<Document>> hash_table;
    size_t cursor = 0;
    std::vector<Document> matched_docs;

    void build_hash_table();

public:
    HashJoinExecutor(std::unique_ptr<AbstractExecutor> out, std::unique_ptr<AbstractExecutor> in,
                     const std::string& out_c, const std::string& in_c);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class SortExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::string sort_col;
    bool ascending;
    std::vector<std::pair<Document, RecordID>> sorted_records;
    size_t cursor = 0;

public:
    SortExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& col, bool asc = true);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

enum class AggType {
    SUM,
    AVG,
    COUNT,
    MIN,
    MAX
};

class AggregationExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::string agg_col;
    std::string group_col;
    AggType type;
    std::vector<Document> agg_results;
    size_t cursor = 0;

    void compute_aggregations();

public:
    AggregationExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& col, AggType t, const std::string& grp = "");
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class ProjectionExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::vector<std::string> select_fields;

public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> ch, const std::vector<std::string>& fields);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class HavingExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::string agg_field;
    QueryOp op;
    Variant val;

public:
    HavingExecutor(std::unique_ptr<AbstractExecutor> ch, const std::string& field, QueryOp o, const Variant& v);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class DistinctExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::vector<std::string> distinct_fields;
    std::vector<Document> unique_docs;
    size_t cursor = 0;

    void build_unique_set();

public:
    DistinctExecutor(std::unique_ptr<AbstractExecutor> ch, const std::vector<std::string>& fields);
    void init() override;
    bool next(Document& doc, RecordID& rid) override;
    void close() override;
};

class QueryPlanner {
private:
    DiskManager& disk_mgr;
    BufferPoolManager& cache_mgr;
    BPlusTreeIndex& index;

public:
    QueryPlanner(DiskManager& dm, BufferPoolManager& cm, BPlusTreeIndex& idx);
    std::unique_ptr<AbstractExecutor> plan_query(const SQLSelectStatement& stmt);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_PLANNER_H
