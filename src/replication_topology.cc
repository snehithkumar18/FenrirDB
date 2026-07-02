#include "replication_topology.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace FenrirDB {

namespace {

std::string trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string action_text(RebalanceActionType type) {
    if (type == RebalanceActionType::ADD_REPLICA) return "add";
    if (type == RebalanceActionType::REMOVE_REPLICA) return "remove";
    if (type == RebalanceActionType::PROMOTE_REPLICA) return "promote";
    if (type == RebalanceActionType::DEMOTE_REPLICA) return "demote";
    return "move";
}

} // namespace

uint32_t ClusterNode::free_units() const {
    if (used_units >= capacity_units) return 0;
    return capacity_units - used_units;
}

bool ClusterNode::accepts_data() const {
    return health == NodeHealth::HEALTHY && role != NodeRole::WITNESS && free_units() > 0;
}

std::string RebalancePlan::describe() const {
    std::ostringstream out;
    out << "rebalance actions=" << actions.size() << " estimated_bytes=" << estimated_bytes << "\n";
    for (const auto& warning : warnings) {
        out << "warning " << warning.subject << ": " << warning.message << "\n";
    }
    for (const auto& action : actions) {
        out << action_text(action.type) << " shard=" << action.shard_id;
        if (!action.from_node.empty()) out << " from=" << action.from_node;
        if (!action.to_node.empty()) out << " to=" << action.to_node;
        out << " units=" << action.estimated_units << "\n";
    }
    return out.str();
}

bool ReplicationTopology::add_node(const ClusterNode& node) {
    if (node.id.empty() || node_map.find(node.id) != node_map.end()) {
        return false;
    }
    node_map[node.id] = node;
    return true;
}

bool ReplicationTopology::update_node(const ClusterNode& node) {
    if (node.id.empty()) return false;
    node_map[node.id] = node;
    return true;
}

bool ReplicationTopology::remove_node(const std::string& node_id) {
    for (const auto& shard_pair : shard_map) {
        if (shard_has_node(shard_pair.second, node_id)) {
            return false;
        }
    }
    return node_map.erase(node_id) > 0;
}

bool ReplicationTopology::add_shard(const ShardPlacement& shard) {
    if (shard.shard_id.empty() || shard_map.find(shard.shard_id) != shard_map.end()) {
        return false;
    }
    shard_map[shard.shard_id] = shard;
    return true;
}

bool ReplicationTopology::update_shard(const ShardPlacement& shard) {
    if (shard.shard_id.empty()) return false;
    shard_map[shard.shard_id] = shard;
    return true;
}

const ClusterNode* ReplicationTopology::get_node(const std::string& node_id) const {
    auto it = node_map.find(node_id);
    if (it == node_map.end()) return nullptr;
    return &it->second;
}

const ShardPlacement* ReplicationTopology::get_shard(const std::string& shard_id) const {
    auto it = shard_map.find(shard_id);
    if (it == shard_map.end()) return nullptr;
    return &it->second;
}

std::vector<ClusterNode> ReplicationTopology::nodes() const {
    std::vector<ClusterNode> out;
    out.reserve(node_map.size());
    for (const auto& pair : node_map) out.push_back(pair.second);
    return out;
}

std::vector<ShardPlacement> ReplicationTopology::shards() const {
    std::vector<ShardPlacement> out;
    out.reserve(shard_map.size());
    for (const auto& pair : shard_map) out.push_back(pair.second);
    return out;
}

std::vector<PlacementIssue> ReplicationTopology::validate(uint32_t replication_factor) const {
    std::vector<PlacementIssue> issues;
    for (const auto& node_pair : node_map) {
        const ClusterNode& node = node_pair.second;
        if (node.capacity_units == 0) {
            issues.push_back({node.id, "node has zero capacity"});
        }
        if (node.used_units > node.capacity_units) {
            issues.push_back({node.id, "node is over capacity"});
        }
        if (node.zone.empty()) {
            issues.push_back({node.id, "node has no zone"});
        }
    }

    for (const auto& shard_pair : shard_map) {
        const ShardPlacement& shard = shard_pair.second;
        if (shard.replicas.size() < replication_factor) {
            issues.push_back({shard.shard_id, "replication factor is below target"});
        }
        size_t leaders = 0;
        std::set<std::string> nodes_seen;
        std::set<std::string> zones_seen;
        for (const auto& replica : shard.replicas) {
            const ClusterNode* node = get_node(replica.node_id);
            if (!node) {
                issues.push_back({shard.shard_id, "replica references unknown node"});
                continue;
            }
            if (!nodes_seen.insert(replica.node_id).second) {
                issues.push_back({shard.shard_id, "duplicate replica node"});
            }
            if (replica.leader) ++leaders;
            if (!node->zone.empty()) zones_seen.insert(node->zone);
            if (node->health == NodeHealth::OFFLINE && replica.voting) {
                issues.push_back({shard.shard_id, "offline node has voting replica"});
            }
        }
        if (leaders != 1 && !shard.replicas.empty()) {
            issues.push_back({shard.shard_id, "shard must have exactly one leader"});
        }
        if (zones_seen.size() < std::min<size_t>(replication_factor, 2)) {
            issues.push_back({shard.shard_id, "replicas do not span enough zones"});
        }
    }
    return issues;
}

std::vector<FailoverDecision> ReplicationTopology::evaluate_failover() const {
    std::vector<FailoverDecision> decisions;
    for (const auto& shard_pair : shard_map) {
        const ShardPlacement& shard = shard_pair.second;
        const ShardReplica* leader = nullptr;
        for (const auto& replica : shard.replicas) {
            if (replica.leader) {
                leader = &replica;
                break;
            }
        }
        if (!leader) continue;
        const ClusterNode* leader_node = get_node(leader->node_id);
        if (leader_node && leader_node->health == NodeHealth::HEALTHY) continue;

        FailoverDecision decision;
        decision.shard_id = shard.shard_id;
        decision.old_leader = leader->node_id;
        uint64_t best_index = 0;
        for (const auto& replica : shard.replicas) {
            if (replica.leader || !replica.voting) continue;
            const ClusterNode* node = get_node(replica.node_id);
            if (!node || node->health != NodeHealth::HEALTHY) continue;
            if (replica.log_index >= best_index) {
                best_index = replica.log_index;
                decision.new_leader = replica.node_id;
            }
        }
        if (decision.new_leader.empty()) {
            decision.safe = false;
            decision.reasons.push_back("no healthy voting replica available");
        } else if (leader && best_index + 32 < leader->log_index) {
            decision.safe = false;
            decision.reasons.push_back("candidate is too far behind");
        } else {
            decision.safe = true;
        }
        decisions.push_back(decision);
    }
    return decisions;
}

RebalancePlan ReplicationTopology::plan_rebalance(uint32_t replication_factor, uint32_t max_actions) const {
    RebalancePlan plan;
    std::vector<PlacementIssue> issues = validate(replication_factor);
    plan.warnings = issues;

    std::vector<ClusterNode> ordered_nodes = nodes();
    std::sort(ordered_nodes.begin(), ordered_nodes.end(),
        [&](const ClusterNode& a, const ClusterNode& b) {
            return node_load_score(a) < node_load_score(b);
        });

    for (const auto& shard_pair : shard_map) {
        if (plan.actions.size() >= max_actions) break;
        const ShardPlacement& shard = shard_pair.second;
        if (shard.replicas.size() < replication_factor) {
            for (const auto& node : ordered_nodes) {
                if (plan.actions.size() >= max_actions) break;
                if (!node.accepts_data() || shard_has_node(shard, node.id)) continue;
                RebalanceAction action;
                action.type = RebalanceActionType::ADD_REPLICA;
                action.shard_id = shard.shard_id;
                action.to_node = node.id;
                action.estimated_units = shard.size_units;
                plan.estimated_bytes += static_cast<uint64_t>(shard.size_units) * 1024ull * 1024ull;
                plan.actions.push_back(action);
                break;
            }
        }

        for (const auto& replica : shard.replicas) {
            if (plan.actions.size() >= max_actions) break;
            const ClusterNode* node = get_node(replica.node_id);
            if (!node || node->health == NodeHealth::HEALTHY) continue;
            for (const auto& target : ordered_nodes) {
                if (!target.accepts_data() || shard_has_node(shard, target.id)) continue;
                RebalanceAction action;
                action.type = RebalanceActionType::MOVE_REPLICA;
                action.shard_id = shard.shard_id;
                action.from_node = replica.node_id;
                action.to_node = target.id;
                action.estimated_units = shard.size_units;
                plan.estimated_bytes += static_cast<uint64_t>(shard.size_units) * 1024ull * 1024ull;
                plan.actions.push_back(action);
                break;
            }
        }
    }

    for (const auto& decision : evaluate_failover()) {
        if (plan.actions.size() >= max_actions) break;
        if (!decision.safe) continue;
        RebalanceAction action;
        action.type = RebalanceActionType::PROMOTE_REPLICA;
        action.shard_id = decision.shard_id;
        action.from_node = decision.old_leader;
        action.to_node = decision.new_leader;
        action.estimated_units = 1;
        plan.actions.push_back(action);
    }
    return plan;
}

std::string ReplicationTopology::describe() const {
    std::ostringstream out;
    out << "nodes=" << node_map.size() << " shards=" << shard_map.size() << "\n";
    for (const auto& pair : node_map) {
        const ClusterNode& node = pair.second;
        out << "node " << node.id << " zone=" << node.zone << " rack=" << node.rack
            << " capacity=" << node.capacity_units << " used=" << node.used_units << "\n";
    }
    for (const auto& pair : shard_map) {
        const ShardPlacement& shard = pair.second;
        out << "shard " << shard.shard_id << " size=" << shard.size_units;
        for (const auto& replica : shard.replicas) {
            out << " " << replica.node_id << (replica.leader ? ":leader" : ":replica");
        }
        out << "\n";
    }
    return out.str();
}

uint32_t ReplicationTopology::node_load_score(const ClusterNode& node) const {
    if (node.capacity_units == 0) return UINT32_MAX;
    uint64_t ratio = static_cast<uint64_t>(node.used_units) * 1000ull / node.capacity_units;
    if (node.health == NodeHealth::DEGRADED) ratio += 500;
    if (node.health == NodeHealth::DRAINING) ratio += 800;
    if (node.health == NodeHealth::OFFLINE) ratio += 2000;
    return static_cast<uint32_t>(std::min<uint64_t>(ratio, UINT32_MAX));
}

std::vector<std::string> ReplicationTopology::candidate_nodes_for(const ShardPlacement& shard) const {
    std::vector<std::string> candidates;
    for (const auto& pair : node_map) {
        if (!pair.second.accepts_data()) continue;
        if (shard_has_node(shard, pair.first)) continue;
        candidates.push_back(pair.first);
    }
    std::sort(candidates.begin(), candidates.end(), [&](const std::string& a, const std::string& b) {
        return node_load_score(node_map.at(a)) < node_load_score(node_map.at(b));
    });
    return candidates;
}

bool ReplicationTopology::shard_has_node(const ShardPlacement& shard, const std::string& node_id) const {
    return std::any_of(shard.replicas.begin(), shard.replicas.end(),
        [&](const ShardReplica& replica) { return replica.node_id == node_id; });
}

bool ReplicationTopologyParser::parse_text(const std::string& text, ReplicationTopology& out) const {
    out = ReplicationTopology();
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts = split(line, ' ');
        if (parts.empty()) continue;
        if (parts[0] == "node" && parts.size() >= 6) {
            ClusterNode node;
            node.id = parts[1];
            node.zone = parts[2];
            node.rack = parts[3];
            node.role = parse_role(parts[4]);
            node.health = parse_health(parts[5]);
            for (size_t i = 6; i < parts.size(); ++i) {
                if (parts[i].find("capacity=") == 0) node.capacity_units = static_cast<uint32_t>(std::stoul(parts[i].substr(9)));
                else if (parts[i].find("used=") == 0) node.used_units = static_cast<uint32_t>(std::stoul(parts[i].substr(5)));
                else if (parts[i].find("tag=") == 0) node.tags.insert(parts[i].substr(4));
            }
            out.update_node(node);
        } else if (parts[0] == "shard" && parts.size() >= 4) {
            ShardPlacement shard;
            shard.shard_id = parts[1];
            shard.size_units = static_cast<uint32_t>(std::stoul(parts[2]));
            for (size_t i = 3; i < parts.size(); ++i) {
                std::vector<std::string> replica_parts = split(parts[i], ':');
                if (replica_parts.empty()) continue;
                ShardReplica replica;
                replica.node_id = replica_parts[0];
                for (size_t r = 1; r < replica_parts.size(); ++r) {
                    if (replica_parts[r] == "leader") replica.leader = true;
                    else if (replica_parts[r] == "nonvoting") replica.voting = false;
                    else if (replica_parts[r].find("log=") == 0) replica.log_index = std::stoull(replica_parts[r].substr(4));
                }
                shard.replicas.push_back(replica);
            }
            out.update_shard(shard);
        }
    }
    return !out.nodes().empty();
}

std::string ReplicationTopologyParser::emit_text(const ReplicationTopology& topology) const {
    std::ostringstream out;
    for (const auto& node : topology.nodes()) {
        out << "node " << node.id << " " << node.zone << " " << node.rack << " "
            << role_text(node.role) << " " << health_text(node.health)
            << " capacity=" << node.capacity_units << " used=" << node.used_units;
        for (const auto& tag : node.tags) out << " tag=" << tag;
        out << "\n";
    }
    for (const auto& shard : topology.shards()) {
        out << "shard " << shard.shard_id << " " << shard.size_units;
        for (const auto& replica : shard.replicas) {
            out << " " << replica.node_id;
            if (replica.leader) out << ":leader";
            if (!replica.voting) out << ":nonvoting";
            out << ":log=" << replica.log_index;
        }
        out << "\n";
    }
    return out.str();
}

NodeRole ReplicationTopologyParser::parse_role(const std::string& text) {
    if (text == "primary") return NodeRole::PRIMARY;
    if (text == "witness") return NodeRole::WITNESS;
    if (text == "analytics") return NodeRole::ANALYTICS;
    return NodeRole::SECONDARY;
}

NodeHealth ReplicationTopologyParser::parse_health(const std::string& text) {
    if (text == "degraded") return NodeHealth::DEGRADED;
    if (text == "offline") return NodeHealth::OFFLINE;
    if (text == "draining") return NodeHealth::DRAINING;
    return NodeHealth::HEALTHY;
}

std::string ReplicationTopologyParser::role_text(NodeRole role) {
    if (role == NodeRole::PRIMARY) return "primary";
    if (role == NodeRole::WITNESS) return "witness";
    if (role == NodeRole::ANALYTICS) return "analytics";
    return "secondary";
}

std::string ReplicationTopologyParser::health_text(NodeHealth health) {
    if (health == NodeHealth::DEGRADED) return "degraded";
    if (health == NodeHealth::OFFLINE) return "offline";
    if (health == NodeHealth::DRAINING) return "draining";
    return "healthy";
}

} // namespace FenrirDB

