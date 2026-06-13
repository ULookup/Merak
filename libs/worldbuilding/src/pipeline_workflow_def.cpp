#include <merak/worldbuilding/pipeline_workflow_def.hpp>
#include <merak/worldbuilding/pipeline.hpp>

namespace merak::worldbuilding {

void from_json(const nlohmann::json& j, ConditionDef& c) {
    j.at("type").get_to(c.type);
    if (j.contains("entity")) j.at("entity").get_to(c.entity);
    if (j.contains("kind_filter")) c.kind_filter = j.at("kind_filter").get<std::string>();
    if (j.contains("op")) c.op = op_from_string(j.at("op").get<std::string>());
    if (j.contains("target_int")) c.target_int = j.at("target_int").get<int>();
    if (j.contains("target_str")) c.target_str = j.at("target_str").get<std::string>();
    if (j.contains("checks")) c.checks = j.at("checks").get<std::vector<std::string>>();
    c.message = j.value("message", "");
    if (j.contains("params")) c.params = j.at("params");
}

void from_json(const nlohmann::json& j, ConditionGroup& g) {
    if (j.contains("operator")) g.operator_type = j.at("operator").get<std::string>();
    j.at("conditions").get_to(g.conditions);
}

void from_json(const nlohmann::json& j, ActionDef& a) {
    j.at("type").get_to(a.type);
    if (j.contains("params")) a.params = j.at("params");
}

void from_json(const nlohmann::json& j, PhaseContextConfig& c) {
    if (j.contains("inject")) j.at("inject").get_to(c.inject);
    if (j.contains("extra")) c.extra = j.at("extra");
}

void from_json(const nlohmann::json& j, AutoLoopDef& l) {
    j.at("entity").get_to(l.entity);
    j.at("target").get_to(l.target);
    j.at("continue_while").get_to(l.continue_while);
}

void from_json(const nlohmann::json& j, PhaseDefinition& p) {
    j.at("key").get_to(p.key);
    j.at("label").get_to(p.label);
    if (j.contains("initial")) j.at("initial").get_to(p.initial);
    if (j.contains("context")) j.at("context").get_to(p.context);
    if (j.contains("allowed_tools")) j.at("allowed_tools").get_to(p.allowed_tools);
    if (j.contains("advance_when")) p.advance_when = j.at("advance_when").get<ConditionGroup>();
    if (j.contains("allowed_retreat")) j.at("allowed_retreat").get_to(p.allowed_retreat);
    if (j.contains("on_enter")) j.at("on_enter").get_to(p.on_enter);
    if (j.contains("on_exit")) j.at("on_exit").get_to(p.on_exit);
    if (j.contains("on_complete")) j.at("on_complete").get_to(p.on_complete);
    if (j.contains("auto_loop")) p.auto_loop = j.at("auto_loop").get<AutoLoopDef>();
}

void from_json(const nlohmann::json& j, PipelineWorkflowDef& w) {
    j.at("name").get_to(w.name);
    if (j.contains("description")) j.at("description").get_to(w.description);
    if (j.contains("version")) j.at("version").get_to(w.version);
    if (j.contains("auto_advance")) j.at("auto_advance").get_to(w.auto_advance);
    if (j.contains("require_confirmation")) j.at("require_confirmation").get_to(w.require_confirmation);
    j.at("phases").get_to(w.phases);
}

void to_json(nlohmann::json& j, const ConditionResult& r) {
    j = {{"message", r.message}, {"met", r.met}, {"extra", r.extra}};
    if (r.current) j["current"] = *r.current;
    if (r.target) j["target"] = *r.target;
}

void to_json(nlohmann::json& j, const ConditionEvalSummary& s) {
    j = {{"phase_key", s.phase_key}, {"all_met", s.all_met}, {"results", s.results}};
}

void to_json(nlohmann::json& j, const PhaseTransitionRecord& r) {
    j = {
        {"id", r.id}, {"world_id", r.world_id},
        {"from_phase", to_string(r.from_phase)}, {"to_phase", to_string(r.to_phase)},
        {"trigger", r.trigger}, {"timestamp", r.timestamp}
    };
    if (r.triggered_by) j["triggered_by"] = *r.triggered_by;
}

void from_json(const nlohmann::json& j, PhaseTransitionRecord& r) {
    j.at("id").get_to(r.id);
    j.at("world_id").get_to(r.world_id);
    r.from_phase = creative_phase_from_string(j.at("from_phase").get<std::string>()).value_or(CreativePhase::Worldbuilding);
    r.to_phase = creative_phase_from_string(j.at("to_phase").get<std::string>()).value_or(CreativePhase::Worldbuilding);
    j.at("trigger").get_to(r.trigger);
    if (j.contains("triggered_by")) r.triggered_by = j.at("triggered_by").get<std::string>();
    j.at("timestamp").get_to(r.timestamp);
}

} // namespace merak::worldbuilding
