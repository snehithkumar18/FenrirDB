#ifndef FENRIRDB_TRANSACTION_MANAGER_H
#define FENRIRDB_TRANSACTION_MANAGER_H

#include <unordered_map>
#include <vector>
#include <mutex>
#include "errors.h"
#include "wal.h"
#include "lock_manager.h"

namespace FenrirDB {

enum class TxState : uint8_t {
    ACTIVE = 0,
    COMMITTED = 1,
    ABORTED = 2
};

struct Transaction {
    uint32_t tx_id;
    uint64_t read_ts;
    uint64_t commit_ts = 0;
    TxState state = TxState::ACTIVE;
    std::vector<RecordID> locks;

    Transaction(uint32_t id, uint64_t r_ts) : tx_id(id), read_ts(r_ts) {}
};

class TransactionManager {
private:
    std::mutex mutex_;
    uint32_t next_tx_id = 1;
    uint64_t global_ts = 1;
    std::unordered_map<uint32_t, std::shared_ptr<Transaction>> tx_table;
    LogManager& log_manager;
    LockManager& lock_manager;

public:
    TransactionManager(LogManager& lm, LockManager& lkm);
    ~TransactionManager() = default;

    std::shared_ptr<Transaction> begin_tx();
    DBErrorCode commit_tx(uint32_t tx_id);
    DBErrorCode abort_tx(uint32_t tx_id, BufferPoolManager& cache_mgr);

    bool is_visible(uint32_t tx_id, uint64_t record_lsn);
    std::shared_ptr<Transaction> get_tx(uint32_t tx_id);
};

} // namespace FenrirDB

#endif // FENRIRDB_TRANSACTION_MANAGER_H
