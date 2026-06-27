#include "../src/query_engine_compiler_recursive.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_recursive_union_basic() {
    std::cout << "Running test_recursive_union_basic..." << std::endl;

    // Anchor: outputs single document {"n": 1}
    std::vector<FenrirDB::Document> anchor_docs;
    {
        FenrirDB::Document d;
        d.set_field("n", FenrirDB::Variant(1));
        anchor_docs.push_back(d);
    }

    class MockAnchorExecutor : public FenrirDB::AbstractExecutor {
    private:
        std::vector<FenrirDB::Document> docs;
        size_t cursor = 0;
    public:
        explicit MockAnchorExecutor(const std::vector<FenrirDB::Document>& d) : docs(d) {}
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

    // Recursive Executor wrapper: reads from parent work table, increments "n" if "n < 3"
    class MockRecursiveExecutor : public FenrirDB::AbstractExecutor {
    private:
        FenrirDB::RecursiveUnionExecutor* parent_union = nullptr;
        size_t cursor = 0;
    public:
        void set_parent(FenrirDB::RecursiveUnionExecutor* p) { parent_union = p; }
        void init() override { cursor = 0; }
        bool next(FenrirDB::Document& doc, FenrirDB::RecordID& rid) override {
            if (!parent_union) return false;
            auto& work = parent_union->get_work_table();

            if (cursor < work.size()) {
                FenrirDB::Document d = work[cursor++];
                FenrirDB::Variant v;
                if (d.get_field("n", v) && v.get_int() < 3) {
                    doc.set_field("n", FenrirDB::Variant(v.get_int() + 1));
                    rid = { 0, 0 };
                    return true;
                }
            }
            return false;
        }
        void close() override {}
    };

    auto anchor = std::make_unique<MockAnchorExecutor>(anchor_docs);
    auto recursive = std::make_unique<MockRecursiveExecutor>();
    auto* rec_ptr = recursive.get();

    auto union_exec = std::make_unique<FenrirDB::RecursiveUnionExecutor>(std::move(anchor), std::move(recursive));
    rec_ptr->set_parent(union_exec.get());

    union_exec->init();

    FenrirDB::Document result;
    FenrirDB::RecordID rid;

    // Verify output: should yield 1, 2, 3
    int count = 0;
    while (union_exec->next(result, rid)) {
        FenrirDB::Variant val;
        assert(result.get_field("n", val));
        count++;
        assert(val.get_int() == count);
    }
    assert(count == 3);
    union_exec->close();

    std::cout << "test_recursive_union_basic passed." << std::endl;
}

int main() {
    test_recursive_union_basic();
    std::cout << "All Recursive CTE tests passed successfully!" << std::endl;
    return 0;
}
