#pragma once
#include <merak/config.hpp>
#include <merak/http_server.hpp>
#include <memory>
#include <filesystem>

namespace pqxx { class connection; }
namespace merak {
class LlmProvider;
class Compactor;
class ToolRegistry;
class MemoryStore;
class EmbeddingProvider;
class ContextAssembler;
class EditJournal;
class SessionStore;
class RuntimeService;
}

namespace merak::worldbuilding {
class WorldbuildingService;
class PipelineManager;
}

namespace merak {
class PortablePg;
class WorldbuildingHttpHandler;
namespace kg { class KnowledgeGraphProvider; }
namespace skills { class SkillRegistry; }
}

namespace merak::app {

class Application {
public:
    struct Options {
        std::filesystem::path home_dir;
        std::filesystem::path exe_dir;
        int port = 3888;
    };

    Application(Config config, Options options);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Start all background services (non-blocking).
    void start();

    // Start HTTP server and block until stopped.
    // Must call start() first.
    void serve();

    // Signal the HTTP server to stop. Safe to call from any thread.
    void stop();

    // Access built components.
    RuntimeService& runtime() { return *runtime_; }
    const RuntimeMetadata& metadata() const { return metadata_; }
    const Options& options() const { return options_; }
    Config& config() { return config_; }
    std::shared_ptr<HttpServer> http_server() { return http_server_; }

private:
    // ---------- initialization phases, called by start() ----------
    void init_portable_db();
    void init_worldbuilding();
    void init_llm();
    void init_tools_phase1();   // platform + deferred tools
    void init_tools_phase2();   // memory/session tools (after memory)
    void init_memory_and_compactor();
    void init_skills();
    void init_runtime();
    void init_pipeline();
    void init_http();
    void init_worldbuilding_http_routes();
    void init_image_service();

    // Factory helpers
    AgentLoop::Config make_loop_config(const std::string& model) const;
    AgentResponse execute_sub_run(
        const SubAgentConfig& agent, const std::string& task,
        RunControl& control, const AgentRunContext& context);

    Config config_;
    Options options_;
    bool started_ = false;

    // Owned components
    std::unique_ptr<PortablePg> portable_pg_;
    std::shared_ptr<pqxx::connection> pg_conn_;
    std::unique_ptr<merak::kg::KnowledgeGraphProvider> kg_provider_;
    std::shared_ptr<worldbuilding::WorldbuildingService> wb_service_;
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<EmbeddingProvider> embedder_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<Compactor> compactor_;
    std::shared_ptr<ContextAssembler> context_assembler_;
    std::shared_ptr<EditJournal> edit_journal_;
    std::shared_ptr<skills::SkillRegistry> skill_registry_;
    std::shared_ptr<SessionStore> session_store_;
    std::shared_ptr<RuntimeService> runtime_;
    std::shared_ptr<merak::worldbuilding::PipelineManager> pipeline_mgr_;
    std::shared_ptr<HttpServer> http_server_;
    std::shared_ptr<WorldbuildingHttpHandler> wb_handler_;
    std::shared_ptr<std::atomic<bool>> plan_mode_;
    RuntimeMetadata metadata_;
};

} // namespace merak::app
