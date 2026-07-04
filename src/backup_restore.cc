#include "backup_restore.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace FenrirDB {

namespace {

std::string trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string hex_hash(const std::string& text) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << h;
    return out.str();
}

bool parse_uint64(const std::string& str, uint64_t& out_val) {
    if (str.empty()) return false;
    size_t processed = 0;
    try {
        out_val = std::stoull(str, &processed);
        return processed == str.size();
    } catch (...) {
        return false;
    }
}

} // namespace

std::string RestorePlan::describe() const {
    std::ostringstream out;
    out << (complete ? "complete" : "incomplete") << " restore bytes=" << total_bytes << "\n";
    for (const auto& issue : issues) {
        out << "issue backup=" << issue.backup_id << " object=" << issue.object_id
            << " " << issue.message << "\n";
    }
    for (const auto& step : steps) {
        out << "restore backup=" << step.source_backup_id << " object=" << step.object_id
            << " table=" << step.target_table << " bytes=" << step.bytes << "\n";
    }
    return out.str();
}

bool BackupCatalog::add_backup(const BackupSet& backup) {
    if (backup.backup_id.empty() || backup_map.find(backup.backup_id) != backup_map.end()) {
        return false;
    }
    backup_map[backup.backup_id] = backup;
    return true;
}

bool BackupCatalog::remove_backup(const std::string& backup_id) {
    for (const auto& pair : backup_map) {
        if (pair.second.parent_backup_id == backup_id) {
            return false;
        }
    }
    return backup_map.erase(backup_id) > 0;
}

const BackupSet* BackupCatalog::find_backup(const std::string& backup_id) const {
    auto it = backup_map.find(backup_id);
    if (it == backup_map.end()) return nullptr;
    return &it->second;
}

std::vector<BackupSet> BackupCatalog::backups() const {
    std::vector<BackupSet> out;
    out.reserve(backup_map.size());
    for (const auto& pair : backup_map) out.push_back(pair.second);
    return out;
}

std::vector<BackupValidationIssue> BackupCatalog::validate_chain(const std::string& backup_id,
                                                                 const Catalog& catalog) const {
    std::vector<BackupValidationIssue> issues;
    std::vector<BackupSet> chain;
    if (!collect_chain(backup_id, chain)) {
        issues.push_back({backup_id, "", "backup chain is incomplete"});
        return issues;
    }
    if (chain.empty()) {
        issues.push_back({backup_id, "", "backup chain is empty"});
        return issues;
    }
    if (chain.front().kind != BackupKind::FULL && chain.front().kind != BackupKind::SNAPSHOT) {
        issues.push_back({chain.front().backup_id, "", "chain does not start with full backup"});
    }

    uint64_t expected_wal = chain.front().wal_end;
    std::set<std::string> seen_objects;
    for (size_t i = 0; i < chain.size(); ++i) {
        const BackupSet& backup = chain[i];
        if (i > 0 && backup.wal_start > expected_wal) {
            issues.push_back({backup.backup_id, "", "wal range has a gap"});
        }
        expected_wal = std::max(expected_wal, backup.wal_end);
        if (backup.catalog_signature.empty()) {
            issues.push_back({backup.backup_id, "", "missing catalog signature"});
        }
        for (const auto& object : backup.objects) {
            if (object.object_id.empty()) {
                issues.push_back({backup.backup_id, "", "object has empty id"});
            }
            if (object.length == 0 && object.kind != BackupObjectKind::MANIFEST) {
                issues.push_back({backup.backup_id, object.object_id, "object has zero length"});
            }
            if (object.kind == BackupObjectKind::TABLE_DATA && !catalog.has_table(object.table)) {
                issues.push_back({backup.backup_id, object.object_id, "object references unknown table"});
            }
            if (!seen_objects.insert(backup.backup_id + ":" + object.object_id).second) {
                issues.push_back({backup.backup_id, object.object_id, "duplicate object id"});
            }
        }
    }
    return issues;
}

RestorePlan BackupCatalog::plan_restore(const std::string& backup_id, const Catalog& catalog,
                                        const std::vector<std::string>& tables) const {
    RestorePlan plan;
    plan.issues = validate_chain(backup_id, catalog);
    if (!plan.issues.empty()) {
        return plan;
    }
    std::vector<BackupSet> chain;
    if (!collect_chain(backup_id, chain)) {
        plan.issues.push_back({backup_id, "", "backup chain cannot be collected"});
        return plan;
    }
    for (const auto& backup : chain) {
        for (const auto& object : backup.objects) {
            if (!object_matches_tables(object, tables)) continue;
            RestoreStep step;
            step.kind = object.kind;
            step.source_backup_id = backup.backup_id;
            step.object_id = object.object_id;
            step.target_table = object.table;
            step.bytes = object.length;
            plan.total_bytes += object.length;
            plan.steps.push_back(step);
        }
    }
    plan.complete = !plan.steps.empty();
    return plan;
}

std::vector<std::string> BackupCatalog::retention_candidates(uint64_t now_ms, uint64_t keep_ms,
                                                             size_t keep_full) const {
    std::vector<BackupSet> sorted = backups();
    std::sort(sorted.begin(), sorted.end(), [](const BackupSet& a, const BackupSet& b) {
        return a.completed_ms > b.completed_ms;
    });
    std::set<std::string> keep;
    size_t full_seen = 0;
    for (const auto& backup : sorted) {
        bool young = now_ms < backup.completed_ms + keep_ms;
        bool full = backup.kind == BackupKind::FULL || backup.kind == BackupKind::SNAPSHOT;
        if (young || (full && full_seen < keep_full)) {
            keep.insert(backup.backup_id);
            if (full) ++full_seen;
        }
    }
    for (const auto& backup : sorted) {
        if (keep.find(backup.parent_backup_id) != keep.end()) {
            keep.insert(backup.backup_id);
        }
    }

    std::vector<std::string> remove;
    for (const auto& backup : sorted) {
        if (keep.find(backup.backup_id) == keep.end()) {
            remove.push_back(backup.backup_id);
        }
    }
    return remove;
}

std::string BackupCatalog::describe() const {
    std::ostringstream out;
    out << "backups=" << backup_map.size() << "\n";
    for (const auto& pair : backup_map) {
        const BackupSet& backup = pair.second;
        out << "backup " << backup.backup_id << " objects=" << backup.objects.size()
            << " wal=" << backup.wal_start << "-" << backup.wal_end
            << " parent=" << backup.parent_backup_id << "\n";
    }
    return out.str();
}

bool BackupCatalog::collect_chain(const std::string& backup_id, std::vector<BackupSet>& chain) const {
    chain.clear();
    std::set<std::string> seen;
    const BackupSet* current = find_backup(backup_id);
    while (current) {
        if (!seen.insert(current->backup_id).second) return false;
        chain.push_back(*current);
        if (current->parent_backup_id.empty()) break;
        current = find_backup(current->parent_backup_id);
    }
    if (!chain.empty() && !chain.back().parent_backup_id.empty()) return false;
    std::reverse(chain.begin(), chain.end());
    return !chain.empty();
}

bool BackupCatalog::object_matches_tables(const BackupObject& object, const std::vector<std::string>& tables) {
    if (tables.empty()) return true;
    if (object.kind == BackupObjectKind::CATALOG || object.kind == BackupObjectKind::MANIFEST ||
        object.kind == BackupObjectKind::CHECKPOINT_IMAGE || object.kind == BackupObjectKind::WAL_SEGMENT) {
        return true;
    }
    return std::find(tables.begin(), tables.end(), object.table) != tables.end();
}

BackupSet BackupPlanner::create_backup_set(const std::string& backup_id, BackupKind kind,
                                           const Catalog& catalog,
                                           const CheckpointRecord& checkpoint,
                                           const std::vector<std::string>& tables,
                                           const std::string& parent_backup_id) const {
    BackupSet backup;
    backup.backup_id = backup_id;
    backup.kind = kind;
    backup.started_ms = checkpoint.timestamp_ms;
    backup.completed_ms = checkpoint.timestamp_ms + 1000;
    backup.base_checkpoint_id = checkpoint.checkpoint_id;
    backup.wal_start = checkpoint.wal_start;
    backup.wal_end = checkpoint.wal_end;
    backup.parent_backup_id = parent_backup_id;
    backup.catalog_signature = signature_for(catalog);

    BackupObject catalog_object;
    catalog_object.object_id = backup_id + "/catalog";
    catalog_object.kind = BackupObjectKind::CATALOG;
    catalog_object.length = catalog.serialize().size();
    catalog_object.checksum = hex_hash(catalog.describe());
    backup.objects.push_back(catalog_object);

    for (const auto& table : catalog.tables()) {
        if (!tables.empty() && std::find(tables.begin(), tables.end(), table.name()) == tables.end()) {
            continue;
        }
        BackupObject data;
        data.object_id = backup_id + "/table/" + table.name();
        data.kind = BackupObjectKind::TABLE_DATA;
        data.table = table.name();
        data.length = estimate_table_bytes(table);
        data.checksum = hex_hash(table.canonical_signature());
        backup.objects.push_back(data);

        for (const auto& index : table.indexes()) {
            BackupObject index_object;
            index_object.object_id = backup_id + "/index/" + table.name() + "/" + index.name;
            index_object.kind = BackupObjectKind::INDEX_DATA;
            index_object.table = table.name();
            index_object.length = std::max<uint64_t>(4096, estimate_table_bytes(table) / 8);
            index_object.checksum = hex_hash(index.name + table.canonical_signature());
            backup.objects.push_back(index_object);
        }
    }

    BackupObject wal;
    wal.object_id = backup_id + "/wal";
    wal.kind = BackupObjectKind::WAL_SEGMENT;
    wal.length = backup.wal_end >= backup.wal_start ? backup.wal_end - backup.wal_start : 0;
    wal.checksum = hex_hash(std::to_string(backup.wal_start) + ":" + std::to_string(backup.wal_end));
    backup.objects.push_back(wal);
    return backup;
}

std::string BackupPlanner::signature_for(const Catalog& catalog) {
    std::ostringstream out;
    for (const auto& table : catalog.tables()) {
        out << table.canonical_signature() << "\n";
    }
    return hex_hash(out.str());
}

uint64_t BackupPlanner::estimate_table_bytes(const TableSchema& table) {
    uint64_t field_weight = std::max<size_t>(1, table.fields().size());
    uint64_t index_weight = std::max<size_t>(1, table.indexes().size());
    return field_weight * index_weight * 64ull * 1024ull;
}

bool BackupManifestParser::parse_text(const std::string& text, BackupCatalog& out) const {
    out = BackupCatalog();
    std::istringstream input(text);
    std::string line;
    BackupSet current;
    bool in_backup = false;

    auto flush = [&]() {
        if (in_backup && !current.backup_id.empty()) {
            out.add_backup(current);
        }
    };

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts = split(line, ' ');
        if (parts.empty()) continue;
        if (parts[0] == "backup" && parts.size() >= 3) {
            flush();
            current = BackupSet();
            current.backup_id = parts[1];
            current.kind = parse_backup_kind(parts[2]);
            in_backup = true;
            for (size_t i = 3; i < parts.size(); ++i) {
                if (parts[i].find("start=") == 0) {
                    uint64_t val;
                    if (parse_uint64(parts[i].substr(6), val)) current.started_ms = val;
                }
                else if (parts[i].find("end=") == 0) {
                    uint64_t val;
                    if (parse_uint64(parts[i].substr(4), val)) current.completed_ms = val;
                }
                else if (parts[i].find("checkpoint=") == 0) {
                    uint64_t val;
                    if (parse_uint64(parts[i].substr(11), val)) current.base_checkpoint_id = val;
                }
                else if (parts[i].find("wal=") == 0) {
                    auto range = split(parts[i].substr(4), '-');
                    if (range.size() == 2) {
                        uint64_t v1, v2;
                        if (parse_uint64(range[0], v1) && parse_uint64(range[1], v2)) {
                            current.wal_start = v1;
                            current.wal_end = v2;
                        }
                    }
                } else if (parts[i].find("parent=") == 0) current.parent_backup_id = parts[i].substr(7);
                else if (parts[i].find("catalog=") == 0) current.catalog_signature = parts[i].substr(8);
            }
        } else if (parts[0] == "object" && in_backup && parts.size() >= 5) {
            BackupObject object;
            object.object_id = parts[1];
            object.kind = parse_object_kind(parts[2]);
            object.table = parts[3] == "-" ? "" : parts[3];
            uint64_t len_val;
            if (parse_uint64(parts[4], len_val)) object.length = len_val;
            for (size_t i = 5; i < parts.size(); ++i) {
                if (parts[i].find("shard=") == 0) object.shard = parts[i].substr(6);
                else if (parts[i].find("offset=") == 0) {
                    uint64_t off_val;
                    if (parse_uint64(parts[i].substr(7), off_val)) object.logical_offset = off_val;
                }
                else if (parts[i].find("checksum=") == 0) object.checksum = parts[i].substr(9);
            }
            current.objects.push_back(object);
        }
    }
    flush();
    return !out.backups().empty();
}

std::string BackupManifestParser::emit_text(const BackupCatalog& catalog) const {
    std::ostringstream out;
    for (const auto& backup : catalog.backups()) {
        out << "backup " << backup.backup_id << " " << backup_kind_text(backup.kind)
            << " start=" << backup.started_ms << " end=" << backup.completed_ms
            << " checkpoint=" << backup.base_checkpoint_id
            << " wal=" << backup.wal_start << "-" << backup.wal_end
            << " catalog=" << backup.catalog_signature;
        if (!backup.parent_backup_id.empty()) out << " parent=" << backup.parent_backup_id;
        out << "\n";
        for (const auto& object : backup.objects) {
            out << "object " << object.object_id << " " << object_kind_text(object.kind)
                << " " << (object.table.empty() ? "-" : object.table)
                << " " << object.length;
            if (!object.shard.empty()) out << " shard=" << object.shard;
            out << " offset=" << object.logical_offset << " checksum=" << object.checksum << "\n";
        }
    }
    return out.str();
}

BackupKind BackupManifestParser::parse_backup_kind(const std::string& text) {
    if (text == "incremental") return BackupKind::INCREMENTAL;
    if (text == "differential") return BackupKind::DIFFERENTIAL;
    if (text == "snapshot") return BackupKind::SNAPSHOT;
    return BackupKind::FULL;
}

BackupObjectKind BackupManifestParser::parse_object_kind(const std::string& text) {
    if (text == "catalog") return BackupObjectKind::CATALOG;
    if (text == "index") return BackupObjectKind::INDEX_DATA;
    if (text == "wal") return BackupObjectKind::WAL_SEGMENT;
    if (text == "checkpoint") return BackupObjectKind::CHECKPOINT_IMAGE;
    if (text == "manifest") return BackupObjectKind::MANIFEST;
    return BackupObjectKind::TABLE_DATA;
}

std::string BackupManifestParser::backup_kind_text(BackupKind kind) {
    if (kind == BackupKind::INCREMENTAL) return "incremental";
    if (kind == BackupKind::DIFFERENTIAL) return "differential";
    if (kind == BackupKind::SNAPSHOT) return "snapshot";
    return "full";
}

std::string BackupManifestParser::object_kind_text(BackupObjectKind kind) {
    if (kind == BackupObjectKind::CATALOG) return "catalog";
    if (kind == BackupObjectKind::INDEX_DATA) return "index";
    if (kind == BackupObjectKind::WAL_SEGMENT) return "wal";
    if (kind == BackupObjectKind::CHECKPOINT_IMAGE) return "checkpoint";
    if (kind == BackupObjectKind::MANIFEST) return "manifest";
    return "table";
}

} // namespace FenrirDB
