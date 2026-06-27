#include "../src/query_engine_compiler_pivot.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_pivot_sum_revenue() {
    std::cout << "Running test_pivot_sum_revenue..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("year", FenrirDB::Variant(2025));
        d1.set_field("dept", FenrirDB::Variant("HR"));
        d1.set_field("revenue", FenrirDB::Variant(5000));
        docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("year", FenrirDB::Variant(2025));
        d2.set_field("dept", FenrirDB::Variant("IT"));
        d2.set_field("revenue", FenrirDB::Variant(12000));
        docs.push_back(d2);

        FenrirDB::Document d3;
        d3.set_field("year", FenrirDB::Variant(2025));
        d3.set_field("dept", FenrirDB::Variant("HR"));
        d3.set_field("revenue", FenrirDB::Variant(3000)); // Duplicate HR group
        docs.push_back(d3);
    }

    class MockVectorExecutor : public FenrirDB::AbstractExecutor {
    private:
        std::vector<FenrirDB::Document> docs;
        size_t cursor = 0;
    public:
        explicit MockVectorExecutor(const std::vector<FenrirDB::Document>& d) : docs(d) {}
        void init() override { cursor = 0; }
        bool next(FenrirDB::Document& doc, FenrirDB::RecordID& rid) override {
            if (cursor < docs.size()) {
                doc = docs[cursor++];
                rid = { 0, 0 };
                return true;
            }
            return false;
        }
        void close() override {}
    };

    auto child = std::make_unique<MockVectorExecutor>(docs);
    FenrirDB::PivotSpec spec;
    spec.group_cols = {"year"};
    spec.pivot_col = "dept";
    spec.value_col = "revenue";
    spec.pivot_values = {"HR", "IT"};

    FenrirDB::PivotExecutor pivot_exec(std::move(child), spec);
    pivot_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    assert(pivot_exec.next(doc, rid) == true);
    
    FenrirDB::Variant year_val, hr_rev, it_rev;
    assert(doc.get_field("year", year_val) && year_val.get_int() == 2025);
    assert(doc.get_field("HR", hr_rev) && hr_rev.get_int() == 8000); // 5000 + 3000
    assert(doc.get_field("IT", it_rev) && it_rev.get_int() == 12000);

    assert(pivot_exec.next(doc, rid) == false);
    pivot_exec.close();

    std::cout << "test_pivot_sum_revenue passed." << std::endl;
}

int main() {
    test_pivot_sum_revenue();
    std::cout << "All Pivot tests passed successfully!" << std::endl;
    return 0;
}
