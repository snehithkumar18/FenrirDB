#include "../src/query_engine_compiler_recursive.h"
#include "../src/query_planner.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_recursive_cte_with_joins() {
    std::cout << "Running test_recursive_cte_with_joins..." << std::endl;

    // Anchor hierarchy entry: {"id": 1, "manager_id": 0} (root node)
    std::vector<FenrirDB::Document> anchor_docs;
    {
        FenrirDB::Document d;
        d.set_field("id", FenrirDB::Variant(1));
        d.set_field("manager_id", FenrirDB::Variant(0));
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

    // Recursive Executor wrapper: increments "id" by 1 up to id < 3
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
                if (d.get_field("id", v) && v.get_int() < 3) {
                    doc.set_field("id", FenrirDB::Variant(v.get_int() + 1));
                    doc.set_field("manager_id", v);
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

    // Join table (employees lookup table)
    std::vector<FenrirDB::Document> emp_lookup;
    {
        FenrirDB::Document e1;
        e1.set_field("emp_id", FenrirDB::Variant(1));
        e1.set_field("name", FenrirDB::Variant("CEO"));
        emp_lookup.push_back(e1);

        FenrirDB::Document e2;
        e2.set_field("emp_id", FenrirDB::Variant(2));
        e2.set_field("name", FenrirDB::Variant("VP"));
        emp_lookup.push_back(e2);
        
        FenrirDB::Document e3;
        e3.set_field("emp_id", FenrirDB::Variant(3));
        e3.set_field("name", FenrirDB::Variant("Manager"));
        emp_lookup.push_back(e3);
    }

    class MockLookupExecutor : public FenrirDB::AbstractExecutor {
    private:
        std::vector<FenrirDB::Document> docs;
        size_t cursor = 0;
    public:
        explicit MockLookupExecutor(const std::vector<FenrirDB::Document>& d) : docs(d) {}
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

    auto lookup_exec = std::make_unique<MockLookupExecutor>(emp_lookup);

    // HashJoin on union_exec.id = lookup_exec.emp_id
    FenrirDB::HashJoinExecutor join_exec(std::move(union_exec), std::move(lookup_exec), "id", "emp_id");
    join_exec.init();

    FenrirDB::Document joined;
    FenrirDB::RecordID rid;

    int match_count = 0;
    while (join_exec.next(joined, rid)) {
        FenrirDB::Variant name_val, id_val;
        assert(joined.get_field("name", name_val));
        assert(joined.get_field("id", id_val));
        match_count++;

        if (id_val.get_int() == 1) assert(name_val.get_string() == "CEO");
        else if (id_val.get_int() == 2) assert(name_val.get_string() == "VP");
        else if (id_val.get_int() == 3) assert(name_val.get_string() == "Manager");
    }
    assert(match_count == 3);
    join_exec.close();

    std::cout << "test_recursive_cte_with_joins passed." << std::endl;
}

int main() {
    test_recursive_cte_with_joins();
    std::cout << "All Recursive CTE and Join tests passed successfully!" << std::endl;
    return 0;
}
