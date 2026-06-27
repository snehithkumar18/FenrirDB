#include "query_engine_compiler_partition_list.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

ListPartitionManager::ListPartitionManager(const std::string& table, const std::string& col)
    : base_table_name(table), partition_col(col) {
    Logger::get_instance().info("PartitionList", "ListPartitionManager initialized on table: " + table + ", column: " + col);
}

void ListPartitionManager::add_partition(const std::string& name, const std::vector<std::string>& vals) {
    partitions.push_back({name, vals});
    
    std::string db_file = base_table_name + "_list_p" + name + ".db";
    auto db = std::make_unique<Database>();
    db->open(db_file);
    partition_dbs[name] = std::move(db);

    std::string values_str = "";
    for (const auto& v : vals) values_str += v + " ";
    Logger::get_instance().info("PartitionList", "Added list partition " + name + " [ " + values_str + "]");
}

DBErrorCode ListPartitionManager::insert(const Document& doc) {
    Variant v;
    if (!doc.get_field(partition_col, v) || v.type != VariantType::STRING) {
        return DBErrorCode::ERR_INVALID_PARAMETER;
    }

    std::string val = v.get_string();
    std::string matched_partition = "";
    for (const auto& p : partitions) {
        if (std::find(p.values.begin(), p.values.end(), val) != p.values.end()) {
            matched_partition = p.partition_name;
            break;
        }
    }

    if (matched_partition.empty()) {
        return DBErrorCode::ERR_GENERIC; // No partition matches list
    }

    // Insert into partition database
    Variant id_v;
    std::string key = "doc_";
    if (doc.get_field("id", id_v)) {
        if (id_v.type == VariantType::INT) key += std::to_string(id_v.get_int());
        else if (id_v.type == VariantType::STRING) key += id_v.get_string();
    }

    Logger::get_instance().info("PartitionList", "Routing insert to partition: " + matched_partition);
    return partition_dbs[matched_partition]->insert(key, doc);
}

std::vector<std::string> ListPartitionManager::prune_partitions(QueryOp op, const Variant& val) {
    std::vector<std::string> target_partitions;
    if (val.type != VariantType::STRING) {
        // Can't prune list without string matching
        for (const auto& p : partitions) {
            target_partitions.push_back(p.partition_name);
        }
        return target_partitions;
    }

    std::string compare_val = val.get_string();
    for (const auto& p : partitions) {
        bool match = false;
        if (op == QueryOp::EQ) {
            if (std::find(p.values.begin(), p.values.end(), compare_val) != p.values.end()) match = true;
        } else {
            // Non-equality operators scan all partitions
            match = true;
        }

        if (match) {
            target_partitions.push_back(p.partition_name);
        }
    }

    Logger::get_instance().info("PartitionList", "Pruned partitions count: " + std::to_string(partitions.size() - target_partitions.size()));
    return target_partitions;
}

} // namespace FenrirDB
