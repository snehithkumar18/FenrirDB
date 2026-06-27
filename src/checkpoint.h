#ifndef FENRIRDB_CHECKPOINT_H
#define FENRIRDB_CHECKPOINT_H

#include <unordered_map>
#include <vector>
#include <mutex>
#include "errors.h"
#include "wal.h"
#include "cache.h"
#include "transaction_manager.h"

namespace FenrirDB {

struct CheckpointData {
    std::unordered_map<uint32_t, uint64_t> dirty_page_table; // page_id -> rec_lsn
    std::unordered_map<uint32_t, uint64_t> active_tx_table;   // tx_id -> begin_lsn

    std::vector<uint8_t> serialize() const;
    static CheckpointData deserialize(const std::vector<uint8_t>& bytes);
};

class CheckpointManager {
private:
    std::mutex mutex_;
    LogManager& log_manager;
    BufferPoolManager& cache_manager;
    TransactionManager& tx_manager;
    
    std::unordered_map<uint32_t, uint64_t> dirty_pages; // rec_lsn map

public:
    CheckpointManager(LogManager& lm, BufferPoolManager& cm, TransactionManager& tm);
    ~CheckpointManager() = default;

    void mark_page_dirty(uint32_t page_id, uint64_t lsn);
    void mark_page_clean(uint32_t page_id);

    uint64_t begin_checkpoint(); // Returns the safe recovery LSN boundary
};

} // namespace FenrirDB

#endif // FENRIRDB_CHECKPOINT_H
