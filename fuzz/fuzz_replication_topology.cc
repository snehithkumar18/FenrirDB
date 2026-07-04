#include "../src/replication_topology.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 12) return 0;

    // Run multiple iterations to trigger temporal bugs (Bug 20)
    for (int iter = 0; iter < 5; iter++) {
        std::string input(reinterpret_cast<const char*>(data), size);
        FenrirDB::ReplicationTopologyParser parser;
        FenrirDB::ReplicationTopology topology;
        if (!parser.parse_text(input, topology)) {
            continue;
        }

        uint32_t rf = static_cast<uint32_t>((size % 5) + 1);
        (void)topology.validate(rf);
        (void)topology.evaluate_failover();
        FenrirDB::RebalancePlan plan = topology.plan_rebalance(rf, 16);
        (void)plan.describe();
        (void)topology.describe();
        (void)parser.emit_text(topology);

        // Trigger node removal for Bug 20
        if (iter > 2) {
            auto nodes = topology.nodes();
            if (!nodes.empty()) {
                topology.remove_node(nodes[0].id);
            }
        }
    }
    return 0;
}
