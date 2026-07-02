#include "../src/catalog.h"
#include "../src/workload_planner.h"

#include <cassert>
#include <iostream>
#include <string>

static FenrirDB::Catalog make_catalog() {
    FenrirDB::CatalogManifestParser parser;
    FenrirDB::Catalog catalog;
    const std::string manifest =
        "table users\n"
        "version 1\n"
        "field id int required unique min=0\n"
        "field email string required maxlen=120\n"
        "field score int default=0 min=0 max=1000\n"
        "field active bool default=true\n"
        "index users_pk btree id unique\n"
        "index users_email hash email sparse include=id,active\n"
        "table events\n"
        "version 1\n"
        "field id int required\n"
        "field user_id int required\n"
        "field kind string required maxlen=32\n"
        "index events_user composite user_id,kind\n";
    assert(parser.parse_text(manifest, catalog));
    return catalog;
}

int main() {
    FenrirDB::Catalog catalog = make_catalog();
    FenrirDB::WorkloadParser parser;
    std::vector<FenrirDB::WorkloadStatement> statements;

    const std::string workload =
        "insert|users|doc=id=7,email='a@example.test',score=42\n"
        "find|users|where=id=7|project=id,email\n"
        "update|users|where=email='a@example.test'|set=score=99\n"
        "aggregate|events|where=user_id=7|agg=id\n";

    assert(parser.parse_text(workload, statements));
    assert(statements.size() == 4);

    FenrirDB::WorkloadPlanner planner(catalog);
    std::vector<FenrirDB::WorkloadPlan> plans = planner.plan_all(statements);
    assert(plans.size() == statements.size());
    for (const auto& plan : plans) {
        assert(plan.accepted);
        assert(!plan.steps.empty());
        assert(plan.total_cost > 0);
        assert(plan.explain().find("accepted") != std::string::npos);
    }

    const FenrirDB::WorkloadPlan& find_plan = plans[1];
    bool used_index = false;
    for (const auto& step : find_plan.steps) {
        if (step.type == FenrirDB::PlanStepType::INDEX_LOOKUP && step.index == "users_pk") {
            used_index = true;
        }
    }
    assert(used_index);

    FenrirDB::WorkloadStatement bad;
    bad.type = FenrirDB::WorkloadStatementType::FIND;
    bad.table = "users";
    FenrirDB::WorkloadPredicate predicate;
    predicate.field = "missing";
    predicate.op = FenrirDB::QueryOp::EQ;
    predicate.value = FenrirDB::Variant(1);
    bad.predicates.push_back(predicate);
    FenrirDB::WorkloadPlan bad_plan = planner.plan(bad);
    assert(!bad_plan.accepted);
    assert(!bad_plan.diagnostics.empty());

    std::cout << "workload planner tests passed\n";
    return 0;
}
