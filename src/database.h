#ifndef FENRIRDB_DATABASE_H
#define FENRIRDB_DATABASE_H

#include <string>
#include <vector>
#include <memory>
#include "storage.h"
#include "cache.h"
#include "index.h"
#include "query.h"

namespace FenrirDB {

class Database {
private:
    std::unique_ptr<DiskManager> disk_manager;
    std::unique_ptr<BufferPoolManager> cache_manager;
    std::unique_ptr<BPlusTreeIndex> index;
    uint32_t root_index_page = 0;
    Page* last_accessed_page_ = nullptr;

public:
    Database() = default;
    ~Database() = default;

    DBErrorCode open(const std::string& filepath);
    void close();

    DiskManager* get_disk_manager() const { return disk_manager.get(); }
    BufferPoolManager* get_cache_manager() const { return cache_manager.get(); }
    BPlusTreeIndex* get_index() const { return index.get(); }

    DBErrorCode insert(const std::string& key, const Document& doc);
    DBErrorCode get(const std::string& key, Document& doc);
    DBErrorCode find_range(const std::string& start_key, const std::string& end_key, std::vector<Document>& results);
    DBErrorCode query(const std::string& query_str, std::vector<Document>& results);
};

} // namespace FenrirDB

#endif // FENRIRDB_DATABASE_H
