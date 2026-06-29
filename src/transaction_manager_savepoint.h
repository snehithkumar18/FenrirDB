#ifndef FENRIRDB_TRANSACTION_MANAGER_SAVEPOINT_H
#define FENRIRDB_TRANSACTION_MANAGER_SAVEPOINT_H

#include "transaction_manager.h"
#include "wal.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace FenrirDB {

struct Savepoint {
    std::string name;
    uint64_t lsn; // Log Sequence Number at savepoint creation
};

class TransactionSavepointManager {
private:
    std::unordered_map<uint32_t, std::vector<Savepoint>> savepoints;
    LogManager* wal_manager;
    TransactionManager* txn_manager;

public:
    TransactionSavepointManager(LogManager* wal, TransactionManager* txn);
    ~TransactionSavepointManager() = default;

    void create_savepoint(uint32_t txn_id, const std::string& name);
    bool rollback_to_savepoint(uint32_t txn_id, const std::string& name);
    bool release_savepoint(uint32_t txn_id, const std::string& name);
};

} // namespace FenrirDB

#endif // FENRIRDB_TRANSACTION_MANAGER_SAVEPOINT_H
