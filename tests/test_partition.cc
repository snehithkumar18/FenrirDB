#include "../src/query_engine_compiler_partition.h"
#include "../src/errors.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <vector>

using FenrirDB::DBErrorCode;

void test_partition_routing_and_pruning() {
    std::cout << "Running test_partition_routing_and_pruning..." << std::endl;
    
    // Clear files
    std::remove("employees_p_low.db");
    std::remove("employees_p_high.db");

    {
        FenrirDB::RangePartitionManager pm("employees", "salary");
        pm.add_partition("p_low", 0, 5000);
        pm.add_partition("p_high", 5001, 15000);

        FenrirDB::Document d1;
        d1.set_field("id", FenrirDB::Variant(1));
        d1.set_field("salary", FenrirDB::Variant(4000));

        FenrirDB::Document d2;
        d2.set_field("id", FenrirDB::Variant(2));
        d2.set_field("salary", FenrirDB::Variant(8000));

        // Insert should route based on salary
        assert(pm.insert(d1) == DBErrorCode::SUCCESS);
        assert(pm.insert(d2) == DBErrorCode::SUCCESS);

        // Pruning checks
        std::vector<std::string> p1 = pm.prune_partitions(FenrirDB::QueryOp::LT, FenrirDB::Variant(4500));
        assert(p1.size() == 1);
        assert(p1[0] == "p_low");

        std::vector<std::string> p2 = pm.prune_partitions(FenrirDB::QueryOp::GT, FenrirDB::Variant(6000));
        assert(p2.size() == 1);
        assert(p2[0] == "p_high");
    }

    std::remove("employees_p_low.db");
    std::remove("employees_p_high.db");
    std::cout << "test_partition_routing_and_pruning passed." << std::endl;
}

int main() {
    test_partition_routing_and_pruning();
    std::cout << "All Partition tests passed successfully!" << std::endl;
    return 0;
}
