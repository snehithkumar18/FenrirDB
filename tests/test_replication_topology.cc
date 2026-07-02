#include "../src/replication_topology.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    const std::string text =
        "node n1 zone-a rack-1 primary healthy capacity=100 used=50 tag=ssd\n"
        "node n2 zone-b rack-2 secondary healthy capacity=100 used=25 tag=ssd\n"
        "node n3 zone-c rack-3 secondary healthy capacity=100 used=10 tag=hdd\n"
        "node n4 zone-a rack-4 secondary offline capacity=100 used=80\n"
        "shard s1 5 n1:leader:log=100 n2:log=99 n3:log=98\n"
        "shard s2 8 n4:leader:log=80 n2:log=80\n";

    FenrirDB::ReplicationTopologyParser parser;
    FenrirDB::ReplicationTopology topology;
    assert(parser.parse_text(text, topology));
    assert(topology.nodes().size() == 4);
    assert(topology.shards().size() == 2);

    auto issues = topology.validate(3);
    assert(!issues.empty());

    auto decisions = topology.evaluate_failover();
    assert(decisions.size() == 1);
    assert(decisions[0].shard_id == "s2");
    assert(decisions[0].safe);
    assert(decisions[0].new_leader == "n2");

    FenrirDB::RebalancePlan plan = topology.plan_rebalance(3, 8);
    assert(!plan.actions.empty());
    assert(plan.describe().find("rebalance") != std::string::npos);

    std::string emitted = parser.emit_text(topology);
    FenrirDB::ReplicationTopology reparsed;
    assert(parser.parse_text(emitted, reparsed));
    assert(reparsed.nodes().size() == topology.nodes().size());

    std::cout << "replication topology tests passed\n";
    return 0;
}
