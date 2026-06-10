#pragma once
#include <string>
#include <vector>

namespace merak::worldbuilding {

struct PipelineWorkflowDef;

struct PipelineValidationError {
    std::string file_path;
    std::string field;
    std::string message;
    enum Severity { ERROR, WARNING } severity;
};

std::vector<PipelineValidationError> validate_workflow_def(
    const PipelineWorkflowDef& def, const std::string& file_path);

PipelineWorkflowDef make_test_workflow();

} // namespace merak::worldbuilding
