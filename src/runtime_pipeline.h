#ifndef FENRIRDB_RUNTIME_PIPELINE_H
#define FENRIRDB_RUNTIME_PIPELINE_H

#include "runtime_expression.h"
#include "runtime_manifest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace FenrirDB {

struct PipelineTable {
    std::string name;
    std::vector<RuntimeRow> rows;
};

enum class PipelineStageType : uint8_t {
    LOAD = 1,
    FILTER = 2,
    PROJECT = 3,
    HASH_JOIN = 4,
    WINDOW = 5,
    PREVIEW = 6
};

struct PipelineStage {
    PipelineStageType type = PipelineStageType::LOAD;
    std::string arg0;
    std::string arg1;
    int amount = 0;
};

class PipelinePlan {
public:
    std::vector<PipelineStage> stages;
    bool parse_text(const std::string& text);
    bool parse_binary(const std::vector<uint8_t>& bytes);
};

class PipelineExecutor {
public:
    PipelineExecutor();

    void add_table(const PipelineTable& table);
    PipelineTable execute(const PipelinePlan& plan, const RuntimeManifest& manifest);
    std::vector<uint8_t> render_preview(const PipelineTable& table, const std::string& mode) const;

private:
    std::vector<PipelineTable> catalog;
    RuntimeExpressionEngine expressions;

    PipelineTable load_table(const std::string& name) const;
    PipelineTable filter_rows(const PipelineTable& input, const std::string& expr) const;
    PipelineTable project_rows(const PipelineTable& input, const std::string& spec) const;
    PipelineTable hash_join(const PipelineTable& left, const PipelineTable& right,
                            const std::string& left_key, const std::string& right_key) const;
    PipelineTable window_rows(const PipelineTable& input, const std::string& key, int width) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_RUNTIME_PIPELINE_H
