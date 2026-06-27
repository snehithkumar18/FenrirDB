#include "../src/query_engine_compiler_pivot_unpivot.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_unpivot_columns_to_rows() {
    std::cout << "Running test_unpivot_columns_to_rows..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("year", FenrirDB::Variant(2025));
        d1.set_field("HR", FenrirDB::Variant(8000));
        d1.set_field("IT", FenrirDB::Variant(12000));
        docs.push_back(d1);
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
    FenrirDB::UnpivotSpec spec;
    spec.group_cols = {"year"};
    spec.unpivot_name_col = "dept";
    spec.unpivot_val_col = "revenue";
    spec.target_cols = {"HR", "IT"};

    FenrirDB::UnpivotExecutor unpivot_exec(std::move(child), spec);
    unpivot_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    int match_count = 0;
    while (unpivot_exec.next(doc, rid)) {
        FenrirDB::Variant year_val, dept_val, rev_val;
        assert(doc.get_field("year", year_val) && year_val.get_int() == 2025);
        assert(doc.get_field("dept", dept_val));
        assert(doc.get_field("revenue", rev_val));

        if (dept_val.get_string() == "HR") {
            assert(rev_val.get_int() == 8000);
            match_count++;
        } else if (dept_val.get_string() == "IT") {
            assert(rev_val.get_int() == 12000);
            match_count++;
        }
    }
    assert(match_count == 2);
    unpivot_exec.close();

    std::cout << "test_unpivot_columns_to_rows passed." << std::endl;
}

int main() {
    test_unpivot_columns_to_rows();
    std::cout << "All Unpivot tests passed successfully!" << std::endl;
    return 0;
}
