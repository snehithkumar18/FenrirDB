#include "query_engine_compiler_partition_hash.h"
#include "logger.h"

namespace FenrirDB {

size_t HashPartitionManager::calculate_hash_bucket(const std::string& key) const {
    // DJB2 String Hashing
    unsigned long hash = 5381;
    for (char c : key) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % num_partitions;
}

HashPartitionManager::HashPartitionManager(const std::string& table, const std::string& col, size_t num_parts)
    : PartitionManager(table, col), num_partitions(num_parts) {
    
    for (size_t i = 0; i < num_partitions; ++i) {
        std::string db_file = base_table_name + "_hash_p" + std::to_string(i) + ".db";
        auto db = std::make_unique<Database>();
        db->open(db_file);
        partition_dbs.push_back(std::move(db));
    }
    
    Logger::get_instance().info("PartitionHash", "HashPartitionManager created with " + std::to_string(num_partitions) + " buckets.");
}

DBErrorCode HashPartitionManager::insert(const Document& doc) {
    Variant v;
    if (!doc.get_field(partition_col, v)) {
        return DBErrorCode::ERR_INVALID_PARAMETER;
    }

    std::string hash_input = "";
    if (v.type == VariantType::STRING) hash_input = v.get_string();
    else if (v.type == VariantType::INT) hash_input = std::to_string(v.get_int());

    size_t bucket = calculate_hash_bucket(hash_input);
    
    Variant id_v;
    std::string key = "doc_";
    if (doc.get_field("id", id_v)) {
        if (id_v.type == VariantType::INT) key += std::to_string(id_v.get_int());
        else if (id_v.type == VariantType::STRING) key += id_v.get_string();
    }

    Logger::get_instance().info("PartitionHash", "Routing insert to hash bucket: " + std::to_string(bucket));
    return partition_dbs[bucket]->insert(key, doc);
}

std::vector<size_t> HashPartitionManager::prune_partitions(QueryOp op, const Variant& val) {
    std::vector<size_t> active_buckets;
    if (op == QueryOp::EQ) {
        std::string hash_input = "";
        if (val.type == VariantType::STRING) hash_input = val.get_string();
        else if (val.type == VariantType::INT) hash_input = std::to_string(val.get_int());

        active_buckets.push_back(calculate_hash_bucket(hash_input));
    } else {
        // Must scan all buckets for range queries
        for (size_t i = 0; i < num_partitions; ++i) {
            active_buckets.push_back(i);
        }
    }
    return active_buckets;
}

} // namespace FenrirDB
