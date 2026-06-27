#ifndef FENRIRDB_TRANSACTION_ISOLATION_H
#define FENRIRDB_TRANSACTION_ISOLATION_H

#include "transaction_manager.h"
#include <string>
#include <vector>

namespace FenrirDB {

enum class IsolationLevel {
    READ_UNCOMMITTED,
    READ_COMMITTED,
    REPEATABLE_READ,
    SERIALIZABLE
};

class TransactionIsolationManager {
private:
    std::unordered_map<uint32_t, IsolationLevel> txn_levels;

public:
    TransactionIsolationManager() = default;
    ~TransactionIsolationManager() = default;

    void set_isolation_level(uint32_t txn_id, IsolationLevel level);
    IsolationLevel get_isolation_level(uint32_t txn_id) const;

    // Checks if a transaction can read a document version based on LSN and isolation level
    bool is_version_visible(uint32_t txn_id, uint64_t version_lsn, uint64_t current_read_ts) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_TRANSACTION_ISOLATION_H
