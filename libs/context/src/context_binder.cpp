#include <merak/context_binder.hpp>
#include <merak/memory_store.hpp>
#include <spdlog/spdlog.h>

namespace merak {

BoundSection ContextBinder::bind_section(const PlannedSection& planned,
                                          const BindSources& sources) const {
  if (planned.token_budget == 0) {
    return {planned.kind, "", 0, false};
  }

  std::string text;
  switch (planned.kind) {
    case SectionKind::Identity:
      text = sources.identity_text ? sources.identity_text() : "";
      break;
    case SectionKind::Constraints:
      text = sources.constraints_text ? sources.constraints_text() : "";
      break;
    case SectionKind::WorldContext:
      text = sources.world_context_text ? sources.world_context_text() : "";
      break;
    case SectionKind::Skills:
      text = sources.skills_text ? sources.skills_text() : "";
      break;
    case SectionKind::ToolSchemas: {
      for (auto& ts : sources.tool_specs) {
        text += ts.name + ": " + ts.description + "\n";
      }
      break;
    }
    case SectionKind::WorkingMemory:
      text = sources.working_memory_text ? sources.working_memory_text() : "";
      break;
    case SectionKind::Memory: {
      if (sources.memory_store && !sources.search_query.empty()) {
        // Fire-and-forget sync stub; in production use co_await
      }
      break;
    }
    case SectionKind::Conversation:
      break;
    case SectionKind::EmergentSkills:
    case SectionKind::EmergentMemory:
    case SectionKind::EmergentSummary:
      break;
  }

  text = truncate_to_budget(text, planned.token_budget);
  int token_est = static_cast<int>(text.size() / 3.5);
  return {planned.kind, std::move(text), token_est, true};
}

std::string ContextBinder::truncate_to_budget(const std::string& text, int budget) {
  if (budget <= 0) return "";
  int char_budget = static_cast<int>(budget * 3.5);
  if (static_cast<int>(text.size()) <= char_budget) return text;
  return text.substr(0, char_budget) + "\n[truncated]";
}

BoundContext ContextBinder::bind(const SectionManifest& manifest,
                                  const BindSources& sources) const {
  BoundContext ctx;
  for (auto& planned : manifest.sections) {
    ctx.sections.push_back(bind_section(planned, sources));
  }
  ctx.provider_messages = sources.conversation_messages;
  ctx.tool_schemas = sources.tool_specs;
  return ctx;
}

} // namespace merak
