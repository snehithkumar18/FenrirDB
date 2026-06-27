#include "../src/query_engine_compiler_partition_list.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <vector>

void test_list_partition_routing() {
    std::cout << "Running test_list_partition_routing..." << std::endl;

    // Clear files
    std::remove("employees_list_pEng.db");
    std::remove("employees_list_pSales.db");

    {
        FenrirDB::ListPartitionManager pm("employees", "dept");
        pm.add_partition("Eng", {"Engineering", "R&D"});
        pm.add_partition("Sales", {"Sales", "Marketing"});

        FenrirDB::Document d1;
        d1.set_field("id", FenrirDB::Variant(1));
        d1.set_field("dept", FenrirDB::Variant("Engineering"));

        FenrirDB::Document d2;
        d2.set_field("id", FenrirDB::Variant(2));
        d2.set_field("dept", FenrirDB::Variant("Sales"));

        assert(pm.insert(d1) == DBErrorCode::SUCCESS);
        assert(pm.insert(d2) == DBErrorCode::SUCCESS);

        // Pruning checks
        std::vector<std::string> p1 = pm.prune_partitions(FenrirDB::QueryOp::EQ, FenrirDB::Variant("Engineering"));
        assert(p1.size() == 1);
        assert(p1[0] == "Eng");

        std::vector<std::string> p2 = pm.prune_partitions(FenrirDB::QueryOp::EQ, FenrirDB::Variant("Sales"));
        assert(p2.size() == 1);
        assert(p2[0] == "Sales");
    }

    std::remove("employees_list_pEng.db");
    std::remove("employees_list_pSales.db");
    std::cout << "test_list_partition_routing passed." << std::endl;
}

int main() {
    test_list_partition_routing();
    std::cout << "All List Partition tests passed successfully!" << std::endl;
    return 0;
}
