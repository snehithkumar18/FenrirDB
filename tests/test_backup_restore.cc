#include "../src/backup_restore.h"
#include "../src/catalog.h"
#include "../src/checkpoint_scheduler.h"

#include <cassert>
#include <iostream>
#include <string>

static FenrirDB::Catalog make_catalog() {
    FenrirDB::CatalogManifestParser parser;
    FenrirDB::Catalog catalog;
    const std::string manifest =
        "table users\n"
        "version 1\n"
        "field id int required unique min=0\n"
        "field email string required maxlen=120\n"
        "field score int default=0 min=0 max=1000\n"
        "index users_pk btree id unique\n";
    assert(parser.parse_text(manifest, catalog));
    return catalog;
}

int main() {
    FenrirDB::Catalog catalog = make_catalog();
    FenrirDB::CheckpointRecord checkpoint;
    checkpoint.checkpoint_id = 3;
    checkpoint.timestamp_ms = 10000;
    checkpoint.wal_start = 100;
    checkpoint.wal_end = 900;
    checkpoint.mode = FenrirDB::CheckpointMode::FULL;
    checkpoint.manifest_hash = "manifest-a";

    FenrirDB::BackupPlanner planner;
    FenrirDB::BackupSet full = planner.create_backup_set(
        "b001", FenrirDB::BackupKind::FULL, catalog, checkpoint, {"users"}, "");
    assert(!full.objects.empty());

    checkpoint.checkpoint_id = 4;
    checkpoint.timestamp_ms = 20000;
    checkpoint.wal_start = 900;
    checkpoint.wal_end = 1200;
    FenrirDB::BackupSet inc = planner.create_backup_set(
        "b002", FenrirDB::BackupKind::INCREMENTAL, catalog, checkpoint, {"users"}, "b001");

    FenrirDB::BackupCatalog backup_catalog;
    assert(backup_catalog.add_backup(full));
    assert(backup_catalog.add_backup(inc));

    auto issues = backup_catalog.validate_chain("b002", catalog);
    assert(issues.empty());

    FenrirDB::RestorePlan restore = backup_catalog.plan_restore("b002", catalog, {"users"});
    assert(restore.complete);
    assert(!restore.steps.empty());
    assert(restore.total_bytes > 0);
    assert(restore.describe().find("restore") != std::string::npos);

    FenrirDB::BackupManifestParser manifest_parser;
    std::string emitted = manifest_parser.emit_text(backup_catalog);
    FenrirDB::BackupCatalog reparsed;
    assert(manifest_parser.parse_text(emitted, reparsed));
    assert(reparsed.find_backup("b001") != nullptr);
    assert(reparsed.find_backup("b002") != nullptr);

    auto remove = backup_catalog.retention_candidates(100000, 1000, 1);
    assert(remove.empty() || remove[0] == "b002");

    std::cout << "backup restore tests passed\n";
    return 0;
}
