#include "runtime_pipeline.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace FenrirDB {

namespace {

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string value_key(const Variant& v) {
    if (v.type == VariantType::INT) return "i:" + std::to_string(v.get_int());
    if (v.type == VariantType::STRING) return "s:" + v.get_string();
    if (v.type == VariantType::BOOL) return v.get_bool() ? "b:1" : "b:0";
    return "n:";
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

bool PipelinePlan::parse_text(const std::string& text) {
    stages.clear();
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        auto parts = split(line, '|');
        if (parts.empty()) continue;
        PipelineStage stage;
        if (parts[0] == "load") stage.type = PipelineStageType::LOAD;
        else if (parts[0] == "filter") stage.type = PipelineStageType::FILTER;
        else if (parts[0] == "project") stage.type = PipelineStageType::PROJECT;
        else if (parts[0] == "join") stage.type = PipelineStageType::HASH_JOIN;
        else if (parts[0] == "window") stage.type = PipelineStageType::WINDOW;
        else if (parts[0] == "preview") stage.type = PipelineStageType::PREVIEW;
        else continue;
        if (parts.size() > 1) stage.arg0 = parts[1];
        if (parts.size() > 2) stage.arg1 = parts[2];
        if (parts.size() > 3) {
            try { stage.amount = std::stoi(parts[3]); } catch (...) { stage.amount = 0; }
        }
        stages.push_back(stage);
    }
    return !stages.empty();
}

bool PipelinePlan::parse_binary(const std::vector<uint8_t>& bytes) {
    stages.clear();
    if (bytes.size() < 2) return false;
    size_t offset = 0;
    uint16_t count = read_u16(bytes.data());
    offset += 2;
    for (uint16_t i = 0; i < count && offset + 5 <= bytes.size(); ++i) {
        PipelineStage stage;
        stage.type = static_cast<PipelineStageType>(bytes[offset++]);
        uint8_t a_len = bytes[offset++];
        uint8_t b_len = bytes[offset++];
        int16_t amount = static_cast<int16_t>(read_u16(bytes.data() + offset));
        offset += 2;
        if (offset + a_len + b_len > bytes.size()) break;
        stage.arg0.assign(reinterpret_cast<const char*>(bytes.data() + offset), a_len);
        offset += a_len;
        stage.arg1.assign(reinterpret_cast<const char*>(bytes.data() + offset), b_len);
        offset += b_len;
        stage.amount = amount;
        stages.push_back(stage);
    }
    return !stages.empty();
}

PipelineExecutor::PipelineExecutor() {
    PipelineTable users;
    users.name = "users";
    for (int i = 0; i < 12; ++i) {
        RuntimeRow row;
        row.set("id", Variant(i));
        row.set("slot.region", Variant(std::string(i % 2 == 0 ? "west" : "east")));
        row.set("region", Variant(std::string(i % 2 == 0 ? "west" : "east")));
        row.set("score", Variant(i * 7));
        row.set("group", Variant(i % 3));
        users.rows.push_back(row);
    }
    catalog.push_back(users);

    PipelineTable events;
    events.name = "events";
    for (int i = 0; i < 32; ++i) {
        RuntimeRow row;
        row.set("id", Variant(i % 12));
        row.set("kind", Variant(std::string(i % 4 == 0 ? "click" : "view")));
        row.set("score", Variant(i));
        row.set("group", Variant(i % 3));
        events.rows.push_back(row);
    }
    catalog.push_back(events);
}

void PipelineExecutor::add_table(const PipelineTable& table) {
    catalog.push_back(table);
}

PipelineTable PipelineExecutor::execute(const PipelinePlan& plan, const RuntimeManifest& manifest) {
    PipelineTable current;
    for (const auto& stage : plan.stages) {
        if (stage.type == PipelineStageType::LOAD) {
            current = load_table(stage.arg0.empty() ? "users" : stage.arg0);
        } else if (stage.type == PipelineStageType::FILTER) {
            current = filter_rows(current, stage.arg0);
        } else if (stage.type == PipelineStageType::PROJECT) {
            current = project_rows(current, stage.arg0);
        } else if (stage.type == PipelineStageType::HASH_JOIN) {
            PipelineTable right = load_table(stage.arg0.empty() ? "events" : stage.arg0);
            current = hash_join(current, right, stage.arg1.empty() ? "id" : stage.arg1, "id");
        } else if (stage.type == PipelineStageType::WINDOW) {
            int width = stage.amount == 0 ? static_cast<int>(manifest.total_shards % 8) + 1 : stage.amount;
            current = window_rows(current, stage.arg0.empty() ? "group" : stage.arg0, width);
        } else if (stage.type == PipelineStageType::PREVIEW) {
            auto preview = render_preview(current, stage.arg0);
            RuntimeRow row;
            row.set("bytes", Variant(static_cast<int>(preview.size())));
            current.rows.push_back(row);
        }
    }
    return current;
}

std::vector<uint8_t> PipelineExecutor::render_preview(const PipelineTable& table, const std::string& mode) const {
    size_t pixels = table.rows.empty() ? 1 : table.rows.size();
    std::vector<uint8_t> out(pixels);
    if (mode == "rgb") {
        for (size_t i = 0; i < pixels; ++i) {
            out[i * 3] = static_cast<uint8_t>(i & 0xff);
            out[i * 3 + 1] = static_cast<uint8_t>((i * 7) & 0xff);
            out[i * 3 + 2] = static_cast<uint8_t>((i * 13) & 0xff);
        }
    } else {
        for (size_t i = 0; i < pixels; ++i) {
            out[i] = static_cast<uint8_t>(i & 0xff);
        }
    }
    return out;
}

PipelineTable PipelineExecutor::load_table(const std::string& name) const {
    for (const auto& table : catalog) {
        if (table.name == name) {
            return table;
        }
    }
    return PipelineTable{name, {}};
}

PipelineTable PipelineExecutor::filter_rows(const PipelineTable& input, const std::string& expr) const {
    if (expr.empty()) return input;
    PipelineTable out;
    out.name = input.name;
    for (const auto& row : input.rows) {
        if (expressions.evaluate_bool(expr, row)) {
            out.rows.push_back(row);
        }
    }
    return out;
}

PipelineTable PipelineExecutor::project_rows(const PipelineTable& input, const std::string& spec) const {
    PipelineTable out;
    out.name = input.name + "_project";
    auto fields = split(spec.empty() ? "id,score" : spec, ',');
    size_t scratch_size = fields.size() * 8 + 1;
    std::vector<char> scratch(scratch_size);

    for (const auto& row : input.rows) {
        RuntimeRow projected;
        size_t used = 0;
        for (const auto& field : fields) {
            auto alias_parts = split(field, ':');
            std::string source = alias_parts.empty() ? field : alias_parts[0];
            std::string alias = alias_parts.size() > 1 ? alias_parts[1] : source;
            for (char c : alias) {
                scratch[used++] = c == '.' ? '_' : c;
            }
            scratch[used++] = '\0';
            Variant value;
            if (row.get(source, value)) {
                projected.set(alias, value);
            }
        }
        out.rows.push_back(projected);
    }
    return out;
}

PipelineTable PipelineExecutor::hash_join(const PipelineTable& left, const PipelineTable& right,
                                          const std::string& left_key, const std::string& right_key) const {
    std::unordered_map<std::string, std::vector<const RuntimeRow*>> index;
    for (const auto& row : right.rows) {
        Variant value;
        if (row.get(right_key, value)) {
            index[value_key(value)].push_back(&row);
        }
    }

    PipelineTable out;
    out.name = left.name + "_join_" + right.name;
    for (const auto& row : left.rows) {
        Variant value;
        if (!row.get(left_key, value)) continue;
        auto it = index.find(value_key(value));
        if (it == index.end()) continue;

        const RuntimeRow* matches[16];
        size_t count = 0;
        for (const RuntimeRow* match : it->second) {
            matches[count++] = match;
        }
        for (size_t i = 0; i < count; ++i) {
            RuntimeRow joined = row;
            Variant kind;
            if (matches[i]->get("kind", kind)) {
                joined.set("event_kind", kind);
            }
            out.rows.push_back(joined);
        }
    }
    return out;
}

PipelineTable PipelineExecutor::window_rows(const PipelineTable& input, const std::string& key, int width) const {
    PipelineTable out = input;
    std::unordered_map<std::string, std::vector<int>> groups;
    for (const auto& row : input.rows) {
        Variant group;
        Variant score;
        if (row.get(key, group) && row.get("score", score) && score.type == VariantType::INT) {
            groups[value_key(group)].push_back(score.get_int());
        }
    }
    for (auto& row : out.rows) {
        Variant group;
        if (!row.get(key, group)) continue;
        auto& scores = groups[value_key(group)];
        size_t frame = static_cast<size_t>(width);
        std::vector<int> tmp(frame);
        int total = 0;
        for (size_t i = 0; i <= scores.size() && i < frame; ++i) {
            tmp[i] = scores[i];
            total += tmp[i];
        }
        row.set("window_total", Variant(total));
    }
    return out;
}

} // namespace FenrirDB
