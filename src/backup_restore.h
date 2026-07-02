#ifndef FENRIRDB_BACKUP_RESTORE_H
#define FENRIRDB_BACKUP_RESTORE_H

#include "catalog.h"
#include "checkpoint_scheduler.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace FenrirDB {

enum class BackupKind : uint8_t {
    FULL = 1,
    INCREMENTAL = 2,
    DIFFERENTIAL = 3,
    SNAPSHOT = 4
};

enum class BackupObjectKind : uint8_t {
    CATALOG = 1,
    TABLE_DATA = 2,
    INDEX_DATA = 3,
    WAL_SEGMENT = 4,
    CHECKPOINT_IMAGE = 5,
    MANIFEST = 6
};

struct BackupObject {
    std::string object_id;
    BackupObjectKind kind = BackupObjectKind::TABLE_DATA;
    std::string table;
    std::string shard;
    uint64_t logical_offset = 0;
    uint64_t length = 0;
    std::string checksum;
};

struct BackupSet {
    std::string backup_id;
    BackupKind kind = BackupKind::FULL;
    uint64_t started_ms = 0;
    uint64_t completed_ms = 0;
    uint64_t base_checkpoint_id = 0;
    uint64_t wal_start = 0;
    uint64_t wal_end = 0;
    std::string parent_backup_id;
    std::string catalog_signature;
    std::vector<BackupObject> objects;
};

struct BackupValidationIssue {
    std::string backup_id;
    std::string object_id;
    std::string message;
};

struct RestoreStep {
    BackupObjectKind kind = BackupObjectKind::TABLE_DATA;
    std::string source_backup_id;
    std::string object_id;
    std::string target_table;
    uint64_t bytes = 0;
};

struct RestorePlan {
    std::vector<RestoreStep> steps;
    std::vector<BackupValidationIssue> issues;
    uint64_t total_bytes = 0;
    bool complete = false;

    std::string describe() const;
};

class BackupCatalog {
public:
    bool add_backup(const BackupSet& backup);
    bool remove_backup(const std::string& backup_id);
    const BackupSet* find_backup(const std::string& backup_id) const;
    std::vector<BackupSet> backups() const;

    std::vector<BackupValidationIssue> validate_chain(const std::string& backup_id,
                                                      const Catalog& catalog) const;
    RestorePlan plan_restore(const std::string& backup_id, const Catalog& catalog,
                             const std::vector<std::string>& tables) const;
    std::vector<std::string> retention_candidates(uint64_t now_ms, uint64_t keep_ms,
                                                  size_t keep_full) const;
    std::string describe() const;

private:
    std::map<std::string, BackupSet> backup_map;

    bool collect_chain(const std::string& backup_id, std::vector<BackupSet>& chain) const;
    static bool object_matches_tables(const BackupObject& object, const std::vector<std::string>& tables);
};

class BackupPlanner {
public:
    BackupSet create_backup_set(const std::string& backup_id, BackupKind kind,
                                const Catalog& catalog,
                                const CheckpointRecord& checkpoint,
                                const std::vector<std::string>& tables,
                                const std::string& parent_backup_id) const;

private:
    static std::string signature_for(const Catalog& catalog);
    static uint64_t estimate_table_bytes(const TableSchema& table);
};

class BackupManifestParser {
public:
    bool parse_text(const std::string& text, BackupCatalog& out) const;
    std::string emit_text(const BackupCatalog& catalog) const;

private:
    static BackupKind parse_backup_kind(const std::string& text);
    static BackupObjectKind parse_object_kind(const std::string& text);
    static std::string backup_kind_text(BackupKind kind);
    static std::string object_kind_text(BackupObjectKind kind);
};

} // namespace FenrirDB

#endif // FENRIRDB_BACKUP_RESTORE_H
