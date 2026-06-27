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
