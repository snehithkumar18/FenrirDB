#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_PASS_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_PASS_H

#include "sql_parser.h"
#include <memory>
#include <vector>

namespace FenrirDB {

class OptimizerPass {
public:
    virtual ~OptimizerPass() = default;
    virtual void run(SQLSelectStatement& stmt) = 0;
};

class ConstantFoldingPass : public OptimizerPass {
public:
    void run(SQLSelectStatement& stmt) override;
};

class PredicatePushdownPass : public OptimizerPass {
public:
    void run(SQLSelectStatement& stmt) override;
};

class JoinReorderingPass : public OptimizerPass {
public:
    void run(SQLSelectStatement& stmt) override;
};

class OptimizationPipeline {
private:
    std::vector<std::unique_ptr<OptimizerPass>> passes;

public:
    OptimizationPipeline();
    ~OptimizationPipeline() = default;

    void add_pass(std::unique_ptr<OptimizerPass> pass);
    void execute(SQLSelectStatement& stmt);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PASS_H
