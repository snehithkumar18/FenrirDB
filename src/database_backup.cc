#include "database_backup.h"
#include "logger.h"
#include <fstream>

namespace FenrirDB {

DatabaseBackup::DatabaseBackup(Database* target_db) : db(target_db) {}

DBErrorCode DatabaseBackup::create_backup(const std::string& backup_filepath) {
    if (!db) return DBErrorCode::ERR_INVALID_PARAMETER;

    Logger::get_instance().info("Backup", "Starting database backup to file: " + backup_filepath);

    std::ofstream backup_file(backup_filepath, std::ios::binary);
    if (!backup_file.is_open()) {
        Logger::get_instance().error("Backup", "Failed to open backup destination file.");
        return DBErrorCode::ERR_GENERIC;
    }

    // Flush dirty pages to disk first to ensure backup consistency
    db->get_cache_manager()->flush_all();

    // Copy database page blocks sequentially
    std::string db_file = "database.db"; // default name
    std::ifstream src_file(db_file, std::ios::binary);
    if (src_file.is_open()) {
        char buffer[PAGE_SIZE];
        while (src_file.read(buffer, PAGE_SIZE)) {
            backup_file.write(buffer, PAGE_SIZE);
        }
        src_file.close();
    }

    backup_file.close();
    Logger::get_instance().info("Backup", "Database backup completed successfully.");
    return DBErrorCode::SUCCESS;
}

DBErrorCode DatabaseBackup::restore_backup(const std::string& backup_filepath) {
    if (!db) return DBErrorCode::ERR_INVALID_PARAMETER;

    Logger::get_instance().info("Backup", "Starting database restore from file: " + backup_filepath);

    std::ifstream backup_file(backup_filepath, std::ios::binary);
    if (!backup_file.is_open()) {
        Logger::get_instance().error("Backup", "Failed to open backup source file.");
        return DBErrorCode::ERR_GENERIC;
    }

    // Close database cache to purge memory pointers
    db->get_cache_manager()->clear();

    std::string db_file = "database.db";
    std::ofstream dest_file(db_file, std::ios::binary | std::ios::trunc);
    if (!dest_file.is_open()) {
        Logger::get_instance().error("Backup", "Failed to open database file for writing.");
        backup_file.close();
        return DBErrorCode::ERR_GENERIC;
    }

    char buffer[PAGE_SIZE];
    while (backup_file.read(buffer, PAGE_SIZE)) {
        dest_file.write(buffer, PAGE_SIZE);
    }

    dest_file.close();
    backup_file.close();

    Logger::get_instance().info("Backup", "Database restored successfully. Reopening database.");
    return DBErrorCode::SUCCESS;
}

} // namespace FenrirDB
