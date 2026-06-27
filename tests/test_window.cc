#include "../src/query_engine_compiler_window.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_row_number_window() {
    std::cout << "Running test_row_number_window..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("dept", FenrirDB::Variant("HR"));
        d1.set_field("val", FenrirDB::Variant(10));
        docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("dept", FenrirDB::Variant("IT"));
        d2.set_field("val", FenrirDB::Variant(20));
        docs.push_back(d2);

        FenrirDB::Document d3;
        d3.set_field("dept", FenrirDB::Variant("HR"));
        d3.set_field("val", FenrirDB::Variant(15));
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
    FenrirDB::WindowSpec spec;
    spec.partition_by_col = "dept";
    spec.order_by_col = "val";
    spec.func = FenrirDB::WindowFuncType::ROW_NUMBER;

    FenrirDB::WindowExecutor window_exec(std::move(child), spec);
    window_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    int match_count = 0;
    while (window_exec.next(doc, rid)) {
        FenrirDB::Variant dept_val, val_val, win_val;
        assert(doc.get_field("dept", dept_val));
        assert(doc.get_field("val", val_val));
        assert(doc.get_field("window_result", win_val));

        if (dept_val.get_string() == "HR" && val_val.get_int() == 10) {
            assert(win_val.get_int() == 1);
            match_count++;
        } else if (dept_val.get_string() == "HR" && val_val.get_int() == 15) {
            assert(win_val.get_int() == 2);
            match_count++;
        } else if (dept_val.get_string() == "IT") {
            assert(win_val.get_int() == 1);
            match_count++;
        }
    }
    assert(match_count == 3);
    window_exec.close();

    std::cout << "test_row_number_window passed." << std::endl;
}

int main() {
    test_row_number_window();
    std::cout << "All Window Function tests passed successfully!" << std::endl;
    return 0;
}
