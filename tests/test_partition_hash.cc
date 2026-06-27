#include "../src/query_engine_compiler_partition_hash.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <vector>

void test_hash_partition_routing() {
    std::cout << "Running test_hash_partition_routing..." << std::endl;

    // Clear files
    std::remove("logs_hash_p0.db");
    std::remove("logs_hash_p1.db");
    std::remove("logs_hash_p2.db");

    {
        FenrirDB::HashPartitionManager pm("logs", "id", 3);

        FenrirDB::Document d1;
        d1.set_field("id", FenrirDB::Variant("user_a"));
        d1.set_field("val", FenrirDB::Variant(10));

        FenrirDB::Document d2;
        d2.set_field("id", FenrirDB::Variant("user_b"));
        d2.set_field("val", FenrirDB::Variant(20));

        assert(pm.insert(d1) == DBErrorCode::SUCCESS);
        assert(pm.insert(d2) == DBErrorCode::SUCCESS);

        // Pruning check
        std::vector<size_t> p1 = pm.prune_partitions(FenrirDB::QueryOp::EQ, FenrirDB::Variant("user_a"));
        assert(p1.size() == 1); // EQ is pruned to exactly one bucket
    }

    std::remove("logs_hash_p0.db");
    std::remove("logs_hash_p1.db");
    std::remove("logs_hash_p2.db");
    std::cout << "test_hash_partition_routing passed." << std::endl;
}

int main() {
    test_hash_partition_routing();
    std::cout << "All Hash Partition tests passed successfully!" << std::endl;
    return 0;
}
