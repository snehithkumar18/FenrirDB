#include "../src/backup_restore.h"
#include "../src/catalog.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 24) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);
    size_t split = input.find("\n---\n");
    if (split == std::string::npos) return 0;

    FenrirDB::CatalogManifestParser catalog_parser;
    FenrirDB::Catalog catalog;
    if (!catalog_parser.parse_text(input.substr(0, split), catalog)) {
        return 0;
    }

    FenrirDB::BackupManifestParser backup_parser;
    FenrirDB::BackupCatalog backups;
    if (!backup_parser.parse_text(input.substr(split + 5), backups)) {
        return 0;
    }

    for (const auto& backup : backups.backups()) {
        (void)backups.validate_chain(backup.backup_id, catalog);
        FenrirDB::RestorePlan plan = backups.plan_restore(backup.backup_id, catalog, {});
        (void)plan.describe();
    }
    (void)backups.retention_candidates(UINT64_MAX / 4, size * 1000, (size % 3) + 1);
    (void)backup_parser.emit_text(backups);
    return 0;
}
