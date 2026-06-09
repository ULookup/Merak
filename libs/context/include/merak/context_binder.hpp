#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace merak {

class MemoryStore;
struct MemorySnippet;

struct BindSources {
  std::function<std::string()> identity_text;
  std::function<std::string()> constraints_text;
  std::function<std::string()> world_context_text;
  std::function<std::string()> skills_text;
  std::vector<ToolSpec> tool_specs;
  std::function<std::string()> working_memory_text;
  std::shared_ptr<MemoryStore> memory_store;
  std::string search_query;
  std::vector<Message> conversation_messages;
};

class ContextBinder {
public:
  ContextBinder() = default;

  BoundContext bind(const SectionManifest& manifest, const BindSources& sources) const;

private:
  BoundSection bind_section(const PlannedSection& planned, const BindSources& sources) const;
  static std::string truncate_to_budget(const std::string& text, int token_budget);
};

} // namespace merak
