#ifndef FENRIRDB_REPLICATION_TOPOLOGY_H
#define FENRIRDB_REPLICATION_TOPOLOGY_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace FenrirDB {

enum class NodeRole : uint8_t {
    PRIMARY = 1,
    SECONDARY = 2,
    WITNESS = 3,
    ANALYTICS = 4
};

enum class NodeHealth : uint8_t {
    HEALTHY = 1,
    DEGRADED = 2,
    OFFLINE = 3,
    DRAINING = 4
};

struct ClusterNode {
    std::string id;
    std::string zone;
    std::string rack;
    NodeRole role = NodeRole::SECONDARY;
    NodeHealth health = NodeHealth::HEALTHY;
    uint32_t capacity_units = 100;
    uint32_t used_units = 0;
    std::set<std::string> tags;

    uint32_t free_units() const;
    bool accepts_data() const;
};

struct ShardReplica {
    std::string node_id;
    bool voting = true;
    bool leader = false;
    uint64_t log_index = 0;
};

struct ShardPlacement {
    std::string shard_id;
    uint32_t size_units = 1;
    std::vector<ShardReplica> replicas;
};

struct PlacementIssue {
    std::string subject;
    std::string message;
};

struct FailoverDecision {
    std::string shard_id;
    std::string old_leader;
    std::string new_leader;
    bool safe = false;
    std::vector<std::string> reasons;
};

enum class RebalanceActionType : uint8_t {
    MOVE_REPLICA = 1,
    ADD_REPLICA = 2,
    REMOVE_REPLICA = 3,
    PROMOTE_REPLICA = 4,
    DEMOTE_REPLICA = 5
};

struct RebalanceAction {
    RebalanceActionType type = RebalanceActionType::MOVE_REPLICA;
    std::string shard_id;
    std::string from_node;
    std::string to_node;
    uint32_t estimated_units = 0;
};

struct RebalancePlan {
    std::vector<RebalanceAction> actions;
    uint64_t estimated_bytes = 0;
    std::vector<PlacementIssue> warnings;

    std::string describe() const;
};

class ReplicationTopology {
public:
    bool add_node(const ClusterNode& node);
    bool update_node(const ClusterNode& node);
    bool remove_node(const std::string& node_id);
    bool add_shard(const ShardPlacement& shard);
    bool update_shard(const ShardPlacement& shard);

    const ClusterNode* get_node(const std::string& node_id) const;
    const ShardPlacement* get_shard(const std::string& shard_id) const;
    std::vector<ClusterNode> nodes() const;
    std::vector<ShardPlacement> shards() const;

    std::vector<PlacementIssue> validate(uint32_t replication_factor) const;
    std::vector<FailoverDecision> evaluate_failover() const;
    RebalancePlan plan_rebalance(uint32_t replication_factor, uint32_t max_actions) const;
    std::string describe() const;

private:
    std::map<std::string, ClusterNode> node_map;
    std::map<std::string, ShardPlacement> shard_map;

    uint32_t node_load_score(const ClusterNode& node) const;
    std::vector<std::string> candidate_nodes_for(const ShardPlacement& shard) const;
    bool shard_has_node(const ShardPlacement& shard, const std::string& node_id) const;
};

class ReplicationTopologyParser {
public:
    bool parse_text(const std::string& text, ReplicationTopology& out) const;
    std::string emit_text(const ReplicationTopology& topology) const;

private:
    static NodeRole parse_role(const std::string& text);
    static NodeHealth parse_health(const std::string& text);
    static std::string role_text(NodeRole role);
    static std::string health_text(NodeHealth health);
};

} // namespace FenrirDB

#endif // FENRIRDB_REPLICATION_TOPOLOGY_H
