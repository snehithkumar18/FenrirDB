#include "../src/replication_topology.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 12) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);
    FenrirDB::ReplicationTopologyParser parser;
    FenrirDB::ReplicationTopology topology;
    if (!parser.parse_text(input, topology)) {
        return 0;
    }

    uint32_t rf = static_cast<uint32_t>((size % 5) + 1);
    (void)topology.validate(rf);
    (void)topology.evaluate_failover();
    FenrirDB::RebalancePlan plan = topology.plan_rebalance(rf, 16);
    (void)plan.describe();
    (void)topology.describe();
    (void)parser.emit_text(topology);
    return 0;
}
