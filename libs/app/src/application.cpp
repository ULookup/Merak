#include <merak/app/application.hpp>
#include <merak/agent_loop.hpp>
#include <merak/anthropic_provider.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/compactor.hpp>
#include <merak/config_loader.hpp>
#include <merak/context_assembler.hpp>
#include <merak/context_pipeline.hpp>
#include <merak/edit_journal.hpp>
#include <merak/http_server.hpp>
#include <merak/mcp_client.hpp>
#include <merak/openai_provider.hpp>
#include <merak/openai_embedding_provider.hpp>
#include <merak/portable_pg.hpp>
#include <merak/portable_neo4j.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/worldbuilding_tools.hpp>
#include <merak/worldbuilding/pipeline_manager.hpp>
#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/skills/skill_registry.hpp>
#include <merak/storage/image_service.hpp>
#include <merak/storage/local_file_image_store.hpp>
#include <merak/tool_registry.hpp>
#include <merak/tool_search_tool.hpp>
#include <merak/git_tool.hpp>
#include <merak/web_fetch_tool.hpp>
#include <merak/web_search_tool.hpp>
#include <merak/lsp_tool.hpp>
#include <merak/symbols_tool.hpp>
#include <merak/memory_tool.hpp>
#include <merak/session_tool.hpp>
#include <merak/agent_tool.hpp>
#include <merak/task_tool.hpp>
#include <merak/ask_user_tool.hpp>
#include <merak/plan_mode_tools.hpp>
#include <merak/worldbuilding_http_handler.hpp>
#include <merak/llm_provider.hpp>
#include <merak/memory_store.hpp>
#include <merak/token_counter.hpp>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>
#include <mutex>

#ifdef MERAK_HAS_KG
#include <merak/kg/neo4j_provider.hpp>
#endif

namespace merak::app {

// ---------- helper: find skill directories ----------

static std::vector<std::filesystem::path> skill_search_dirs(
    const std::filesystem::path& home_dir,
    const std::filesystem::path& exe_dir)
{
    std::vector<std::filesystem::path> dirs;
    if (auto* rd = std::getenv("MERAK_RESOURCE_DIR")) {
        dirs.emplace_back(std::filesystem::path(rd) / "skills");
        dirs.emplace_back(std::filesystem::path(rd) / "config" / "skills");
    }
    if (!exe_dir.empty()) {
        dirs.emplace_back(exe_dir / "skills");
        dirs.emplace_back(exe_dir / "config" / "skills");
        dirs.emplace_back(exe_dir / ".." / "config" / "skills");
    }
    dirs.emplace_back(std::filesystem::current_path() / "config" / "skills");
    dirs.emplace_back(home_dir / "skills");
    return dirs;
}

// ---------- Application ----------

Application::Application(Config config, Options options)
    : config_(std::move(config)), options_(std::move(options))
{}

Application::~Application() {
    if (http_server_) http_server_->stop();
}

void Application::start() {
    if (started_) return;

    init_portable_db();
    init_portable_neo4j();
    init_worldbuilding();
    init_llm();
    init_tools_phase1();
    init_memory_and_compactor();
    init_tools_phase2();
    init_skills();
    init_runtime();
    init_pipeline();
    init_http();
    init_worldbuilding_http_routes();

    started_ = true;
}

void Application::serve() {
    if (!started_) start();
    std::cout << "merak serve listening on 127.0.0.1:" << options_.port << "\n";
    http_server_->listen(options_.port);
}

void Application::stop() {
    if (http_server_) http_server_->stop();
}

// ─────────────────────────────────────────────────────
// init_portable_db
// ─────────────────────────────────────────────────────

void Application::init_portable_db() {
    auto pg_bin = options_.exe_dir / "pg";
    if (!options_.exe_dir.empty() && std::filesystem::exists(pg_bin)
        && config_.memory.db_connection.empty())
    {
        portable_pg_ = std::make_unique<PortablePg>(pg_bin);
        if (portable_pg_->start()) {
            config_.memory.db_connection = portable_pg_->connection_string();
            config_.memory.enabled = true;
            std::cout << "Portable PostgreSQL started on port "
                      << portable_pg_->port() << "\n";
        } else {
            std::cerr << "Warning: portable PostgreSQL failed to start\n";
            portable_pg_.reset();
        }
    }
}

// ─────────────────────────────────────────────────────
// init_portable_neo4j
// ─────────────────────────────────────────────────────

void Application::init_portable_neo4j() {
    if (!config_.knowledge_graph.enabled) return;

    auto neo4j_bin = options_.exe_dir / "neo4j";
    auto java_bin = options_.exe_dir / "java";
    if (options_.exe_dir.empty() ||
        !std::filesystem::exists(neo4j_bin) ||
        !std::filesystem::exists(java_bin)) {
        std::cerr << "Warning: portable Neo4j or Java not found at "
                  << options_.exe_dir << " (neo4j/ and java/ expected)\n";
        return;
    }

    portable_neo4j_ = std::make_unique<PortableNeo4j>(neo4j_bin, java_bin);
    if (portable_neo4j_->start()) {
        config_.knowledge_graph.neo4j_uri = portable_neo4j_->bolt_uri();
        config_.knowledge_graph.neo4j_user = portable_neo4j_->user();
        config_.knowledge_graph.neo4j_password = portable_neo4j_->password();
        std::cout << "Portable Neo4j started on port "
                  << portable_neo4j_->bolt_port() << " (Bolt), "
                  << portable_neo4j_->http_port() << " (HTTP)\n";
    } else {
        std::cerr << "Warning: portable Neo4j failed to start\n";
        portable_neo4j_.reset();
        config_.knowledge_graph.enabled = false;
    }
}

// ─────────────────────────────────────────────────────
// init_worldbuilding
// ─────────────────────────────────────────────────────

void Application::init_worldbuilding() {
    if (config_.memory.db_connection.empty()) return;

    if (config_.knowledge_graph.enabled) {
#ifdef MERAK_HAS_KG
        try {
            kg_provider_ = std::make_unique<merak::kg::Neo4jKGProvider>(
                config_.knowledge_graph.neo4j_uri,
                config_.knowledge_graph.neo4j_user,
                config_.knowledge_graph.neo4j_password,
                config_.knowledge_graph.neo4j_database);
            std::cout << "Knowledge Graph: connected to Neo4j at "
                      << config_.knowledge_graph.neo4j_uri << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Warning: Knowledge Graph unavailable: " << e.what() << "\n";
            kg_provider_.reset();
        }
#else
        std::cerr << "Warning: Knowledge Graph not compiled in (libneo4j-client missing)\n";
#endif
    }

    try {
        wb_service_ = std::make_shared<worldbuilding::WorldbuildingService>(
            config_.memory.db_connection, options_.home_dir / "worldbuilding",
            std::move(kg_provider_));
        wb_service_->initialize();
        wb_service_->set_diary_context_limit(config_.memory.diary_context_limit);
    } catch (const std::exception& e) {
        std::cerr << "Warning: WorldbuildingService not available: " << e.what() << "\n";
        wb_service_.reset();
    }
}

// ─────────────────────────────────────────────────────
// init_llm
// ─────────────────────────────────────────────────────

void Application::init_llm() {
    auto& llm_cfg = config_.llm;
    llm_ = (llm_cfg.provider == "anthropic")
        ? std::static_pointer_cast<LlmProvider>(
            std::make_shared<AnthropicProvider>(llm_cfg))
        : std::static_pointer_cast<LlmProvider>(
            std::make_shared<OpenAIProvider>(llm_cfg));
}

// ─────────────────────────────────────────────────────
// init_tools_phase1 — platform, deferred, plan-mode,
//                     worldbuilding, and MCP tools.
// ─────────────────────────────────────────────────────

void Application::init_tools_phase1() {
    tools_ = std::make_shared<ToolRegistry>();
    tools_->register_platform_basics();
    tools_->register_tool(std::make_unique<tools::MultiEditTool>());
    tools_->register_tool(std::make_unique<tools::DeleteFileTool>());
    tools_->set_permission_mode(config_.agent.permission_mode);

    // Pinned meta-tool
    tools_->register_tool(std::make_unique<tools::ToolSearchTool>(tools_));

    // Deferred platform tools
    tools_->register_tool(std::make_unique<tools::GitTool>());
    tools_->register_tool(std::make_unique<tools::WebFetchTool>());
    tools_->register_tool(std::make_unique<tools::WebSearchTool>());
    tools_->register_tool(std::make_unique<tools::LspTool>());
    tools_->register_tool(std::make_unique<tools::SymbolsTool>());
    tools_->register_tool(std::make_unique<tools::TaskTool>());
    tools_->register_tool(std::make_unique<tools::AskUserTool>());

    // Plan mode
    plan_mode_ = std::make_shared<std::atomic<bool>>(false);

    // SessionStore (needed by ExitPlanModeTool)
    if (!config_.memory.db_connection.empty()) {
        pg_conn_ = std::make_shared<pqxx::connection>(config_.memory.db_connection);
    }
    session_store_ = std::make_shared<SessionStore>(pg_conn_);
    session_store_->set_root(options_.home_dir);
    session_store_->initialize();

    tools_->register_tool(std::make_unique<tools::EnterPlanModeTool>(plan_mode_));
    tools_->register_tool(
        std::make_unique<tools::ExitPlanModeTool>(plan_mode_, session_store_));

    // Worldbuilding tools
    if (wb_service_) {
        worldbuilding::WorldbuildingTools wb_tools(
            *wb_service_, llm_, config_.memory.diary_compression_threshold,
            config_.memory.diary_model, config_.memory.writer_model);
        auto god_tools = wb_tools.create_tools(worldbuilding::AgentKind::God);
        tools_->register_all(std::move(god_tools));
    }

    // MCP tools
    std::vector<McpServerStatus> mcp_status;
    for (auto& mc : config_.mcp_servers) {
        if (!mc.enabled) continue;
        auto c = std::make_shared<McpClient>(mc);
        auto connected = c->connect();
        mcp_status.push_back({mc.name, connected.has_value()});
        if (connected) {
            tools_->import_from_mcp(c).get();
        }
    }
}

// ─────────────────────────────────────────────────────
// init_memory_and_compactor
// ─────────────────────────────────────────────────────

void Application::init_memory_and_compactor() {
    auto memory_cfg = config_.memory;
    if (memory_cfg.db_connection.empty()) memory_cfg.enabled = false;

    // Embedding provider
    if (memory_cfg.enabled && !memory_cfg.embedding_api_key.empty()) {
        OpenAIEmbeddingProvider::Config embed_cfg;
        embed_cfg.api_url     = memory_cfg.embedding_api_url;
        embed_cfg.api_key     = memory_cfg.embedding_api_key;
        embed_cfg.model       = memory_cfg.embedding_model;
        embed_cfg.cache_size  = memory_cfg.embedding_cache_size;
        embed_cfg.batch_size  = memory_cfg.embedding_batch_size;
        embed_cfg.timeout_ms  = memory_cfg.embedding_timeout_ms;
        embedder_ = std::make_shared<OpenAIEmbeddingProvider>(embed_cfg);
        std::cout << "Embedding: using " << memory_cfg.embedding_model
                  << " via " << memory_cfg.embedding_api_url << "\n";
    } else if (memory_cfg.enabled) {
        std::cerr << "Warning: Memory enabled but embedding_api_key not set, "
                     "semantic search disabled\n";
        memory_cfg.enabled = false;
    }

    memory_ = std::make_shared<MemoryStore>(memory_cfg, embedder_);
    if (memory_cfg.enabled) memory_->init_db();

    auto counter = std::make_shared<TokenCounter>();
    TokenBudget budget{128000, config_.agent.reserve_ratio,
                       config_.agent.memory_budget_ratio};
    context_assembler_ = std::make_shared<ContextAssembler>(budget, counter);
    compactor_ = std::make_shared<Compactor>(llm_, counter);
    edit_journal_ = std::make_shared<EditJournal>();
}

// ─────────────────────────────────────────────────────
// init_tools_phase2 — memory-dependent tools
// ─────────────────────────────────────────────────────

void Application::init_tools_phase2() {
    tools_->register_tool(std::make_unique<tools::MemoryTool>(memory_));
    tools_->register_tool(
        std::make_unique<tools::SessionTool>(memory_, compactor_, edit_journal_.get()));
}

// ─────────────────────────────────────────────────────
// init_skills
// ─────────────────────────────────────────────────────

void Application::init_skills() {
    skill_registry_ = std::make_shared<skills::SkillRegistry>();
    for (auto& dir : skill_search_dirs(options_.home_dir, options_.exe_dir)) {
        skill_registry_->discover_from(dir);
    }
    skills::register_fork_skills(
        *skill_registry_, tools_, llm_, memory_,
        config_.llm.default_model, wb_service_, skill_registry_);
}

// ─────────────────────────────────────────────────────
// make_loop_config / execute_sub_run — factory helpers
// ─────────────────────────────────────────────────────

AgentLoop::Config Application::make_loop_config(const std::string& model) const {
    AgentLoop::Config c;
    c.system_prompt     = config_.agent.system_prompt;
    c.max_turns         = config_.agent.max_tool_turns;
    c.default_model     = model.empty() ? config_.llm.default_model : model;
    c.max_output_tokens = config_.llm.max_output_tokens;
    return c;
}

AgentResponse Application::execute_sub_run(
    const SubAgentConfig& agent, const std::string& task,
    RunControl& control, const AgentRunContext& context)
{
    auto sub_tools = std::make_shared<ToolRegistry>();
    sub_tools->set_permission_mode(config_.agent.permission_mode);

    if (agent.tool_allowlist.empty()) {
        for (auto& spec : tools_->all_tools()) {
            if (auto* t = tools_->get_tool(spec.name))
                sub_tools->register_tool(t->clone());
        }
    } else {
        for (auto& name : agent.tool_allowlist) {
            if (auto* t = tools_->get_tool(name))
                sub_tools->register_tool(t->clone());
        }
    }

    auto counter = std::make_shared<TokenCounter>();
    auto sub_compactor = std::make_shared<Compactor>(llm_, counter);

    auto cfg = make_loop_config(agent.model);
    cfg.system_prompt = agent.system_prompt.empty()
        ? config_.agent.system_prompt : agent.system_prompt;

    auto loop = std::make_unique<AgentLoop>(
        cfg, llm_, sub_tools, memory_, sub_compactor,
        wb_service_, skill_registry_);
    loop->set_plan_mode_source(plan_mode_);
    if (!context.world_id.empty())      loop->set_active_world_id(context.world_id);
    if (!context.scene_id.empty())      loop->set_active_scene_id(context.scene_id);
    if (!context.caller_agent_id.empty()) loop->set_caller_agent_id(context.caller_agent_id);

    return loop->run(task, control).get();
}

// ─────────────────────────────────────────────────────
// init_runtime
// ─────────────────────────────────────────────────────

void Application::init_runtime() {
    // Sub-executor delegates to the canonical execute_sub_run method.
    // Capturing 'this' is safe because tools_ (which stores AgentTool holding
    // this lambda) is destroyed before Application members during destruction.
    auto sub_executor = [this](const SubAgentConfig& agent, const std::string& task,
                                RunControl& control, const AgentRunContext& context)
        -> AgentResponse
    {
        return execute_sub_run(agent, task, control, context);
    };

    // Register AgentTool before creating RuntimeService (SubExecutor is
    // passed to the RuntimeService constructor).
    tools_->register_tool(std::make_unique<tools::AgentTool>(
        config_.agent.sub_agents,
        [sub_executor](const SubAgentConfig& agent, const std::string& task,
                        RunControl& control) -> std::string {
            AgentRunContext ctx;
            ctx.caller_agent_id = agent.id;
            return sub_executor(agent, task, control, ctx).text;
        }));

    // LoopFactory — constructs an AgentLoop per run
    auto factory = [cfg = config_, llm = llm_, tools = tools_,
                    memory = memory_, compactor = compactor_,
                    wb = wb_service_, skills = skill_registry_,
                    plan_mode = plan_mode_](const std::string& model)
        -> std::unique_ptr<AgentLoop>
    {
        AgentLoop::Config c;
        c.system_prompt     = cfg.agent.system_prompt;
        c.max_turns         = cfg.agent.max_tool_turns;
        c.default_model     = model.empty() ? cfg.llm.default_model : model;
        c.max_output_tokens = cfg.llm.max_output_tokens;

        auto loop = std::make_unique<AgentLoop>(
            c, llm, tools, memory, compactor, wb, skills);
        loop->set_plan_mode_source(plan_mode);
        return loop;
    };

    runtime_ = std::make_shared<RuntimeService>(
        session_store_, factory, config_.agent.sub_agents, sub_executor);
    runtime_->initialize();
    if (wb_service_) runtime_->set_worldbuilding_service(wb_service_);
    runtime_->set_skill_registry(skill_registry_);
}

// ─────────────────────────────────────────────────────
// init_pipeline
// ─────────────────────────────────────────────────────

void Application::init_pipeline() {
    if (!wb_service_ || config_.memory.db_connection.empty()) return;

    auto condition_evaluator =
        worldbuilding::ConditionEvaluator::create_default();
    if (wb_service_->kg_provider()) {
        condition_evaluator->set_kg_provider(wb_service_->kg_provider());
    }

    auto db_connection = config_.memory.db_connection;
    auto exe_dir = options_.exe_dir;
    pipeline_mgr_ = std::make_shared<worldbuilding::PipelineManager>(
        worldbuilding::PipelineManager::Dependencies{
            .pg_connection_factory = [db_connection]() {
                return std::make_shared<pqxx::connection>(db_connection);
            },
            // Raw pointer to runtime_ is safe because pipeline_mgr_ is declared
            // after runtime_ in the header — it's destroyed first on ~Application.
            .event_emitter = [rt = runtime_.get()](const RuntimeEvent& e) {
                auto wid = e.payload.value("world_id", "");
                if (!wid.empty()) {
                    rt->broadcast_to_world(wid, e);
                } else if (!e.session_id.empty()) {
                    rt->emit_event(e.session_id, e.run_id, e.type, e.payload);
                }
            },
            .invoke_agent = [rt = runtime_.get()](
                const std::string& world_id,
                const std::string& agent_id,
                const std::string& task)
            {
                worldbuilding::PipelineManager::AgentInvocationResult out;
                try {
                    auto session = rt->create_session(
                        "pipeline:" + agent_id, world_id, agent_id);
                    auto run = rt->start_run(session.id, task);
                    out.success = true;
                    out.output = run.id;
                } catch (const std::exception& e) {
                    out.success = false;
                    out.error = e.what();
                }
                return out;
            },
            .pipeline_config_dir = [exe_dir] {
                auto primary = exe_dir / "pipelines";
                if (!exe_dir.empty() && std::filesystem::exists(primary))
                    return primary;
                return exe_dir / ".." / "config" / "pipelines";
            }(),
            .worlds_base_dir = options_.home_dir / "worldbuilding",
            .condition_evaluator = condition_evaluator,
        }
    );
    pipeline_mgr_->initialize();
    runtime_->set_pipeline_manager(pipeline_mgr_);
}

// ─────────────────────────────────────────────────────
// init_http
// ─────────────────────────────────────────────────────

void Application::init_http() {
    // Build metadata
    auto memory_cfg = config_.memory;
    if (memory_cfg.db_connection.empty()) memory_cfg.enabled = false;

    metadata_.provider           = config_.llm.provider;
    metadata_.model              = config_.llm.default_model;
    metadata_.models             = config_.models;
    metadata_.permission_mode    = config_.agent.permission_mode;
    metadata_.memory_enabled     = memory_cfg.enabled;
    metadata_.worldbuilding_enabled = wb_service_ != nullptr;
    metadata_.tools              = tools_->all_tools();
    metadata_.agents             = runtime_->agents();

    http_server_ = std::make_shared<HttpServer>(
        runtime_, metadata_, options_.home_dir.string(), llm_);

    // Serve static WebUI
    auto webui_path = options_.exe_dir / "webui";
    if (!options_.exe_dir.empty() && std::filesystem::exists(webui_path)) {
        http_server_->serve_static_dir("/", webui_path.string());
        std::cout << "Serving WebUI from " << webui_path << "\n";
    }
}

// ─────────────────────────────────────────────────────
// init_worldbuilding_http_routes
// ─────────────────────────────────────────────────────

void Application::init_worldbuilding_http_routes() {
    if (!wb_service_) return;

    wb_handler_ = std::make_shared<WorldbuildingHttpHandler>(
        wb_service_, runtime_);
    if (pipeline_mgr_) wb_handler_->set_pipeline_manager(pipeline_mgr_);

    init_image_service();
    wb_handler_->install_routes(http_server_->raw_server());
}

void Application::init_image_service() {
    if (config_.memory.db_connection.empty()) return;

    auto images_dir  = (options_.home_dir / "data" / "images").string();
    auto uploads_dir = (options_.home_dir / "data" / "uploads" / "chunks").string();
    std::filesystem::create_directories(images_dir);
    std::filesystem::create_directories(uploads_dir);

    auto image_store = std::make_shared<merak::LocalFileImageStore>(
        images_dir, "/api/worldbuilding/images");

    auto conn = std::make_shared<pqxx::connection>(config_.memory.db_connection);
    auto conn_mutex = std::make_shared<std::mutex>();

    auto translate_sql = [](
        const std::string& sql, const nlohmann::json& params,
        std::string& translated, pqxx::params& pq_params)
    {
        static const std::regex param_re(R"(@([a-zA-Z_][a-zA-Z0-9_]*))");
        std::vector<std::string> param_names;
        translated.clear();
        size_t pos = 0;
        auto it = std::sregex_iterator(sql.begin(), sql.end(), param_re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            param_names.push_back((*it)[1].str());
            translated += sql.substr(pos, it->position() - pos);
            translated += "$" + std::to_string(param_names.size());
            pos = it->position() + it->length();
        }
        translated += sql.substr(pos);

        for (auto& name : param_names) {
            auto p = params.find(name);
            if (p == params.end()) { pq_params.append(std::string{}); continue; }
            auto& val = *p;
            switch (val.type()) {
            case nlohmann::json::value_t::string:
                pq_params.append(val.get<std::string>()); break;
            case nlohmann::json::value_t::boolean:
                pq_params.append(val.get<bool>()); break;
            case nlohmann::json::value_t::number_integer:
            case nlohmann::json::value_t::number_unsigned:
                pq_params.append(val.get<long long>()); break;
            case nlohmann::json::value_t::number_float:
                pq_params.append(val.get<double>()); break;
            default:
                pq_params.append(std::string{}); break;
            }
        }
    };

    auto db_query = [conn, conn_mutex, translate_sql](
        const std::string& sql, const nlohmann::json& params) -> nlohmann::json
    {
        std::lock_guard<std::mutex> lock(*conn_mutex);
        pqxx::work txn(*conn);
        std::string translated;
        pqxx::params pq_params;
        translate_sql(sql, params, translated, pq_params);
        auto res = txn.exec_params(translated, pq_params);
        txn.commit();
        nlohmann::json result = nlohmann::json::array();
        for (auto const& row : res) {
            nlohmann::json obj;
            for (int i = 0; i < static_cast<int>(res.columns()); ++i) {
                if (row[i].is_null()) {
                    obj[res.column_name(i)] = nullptr;
                    continue;
                }
                switch (row.column_type(i)) {
                case 16:   obj[res.column_name(i)] = row[i].as<bool>(); break;
                case 20: case 21: case 23:
                    obj[res.column_name(i)] = row[i].as<long long>(); break;
                case 700: case 701: case 1700:
                    obj[res.column_name(i)] = row[i].as<double>(); break;
                default:
                    obj[res.column_name(i)] = row[i].c_str(); break;
                }
            }
            result.push_back(obj);
        }
        return result;
    };

    auto db_exec = [conn, conn_mutex, translate_sql](
        const std::string& sql, const nlohmann::json& params) -> bool
    {
        std::lock_guard<std::mutex> lock(*conn_mutex);
        pqxx::work txn(*conn);
        std::string translated;
        pqxx::params pq_params;
        translate_sql(sql, params, translated, pq_params);
        txn.exec_params(translated, pq_params);
        txn.commit();
        return true;
    };

    auto image_service = std::make_shared<merak::ImageService>(
        image_store, db_query, db_exec,
        [] { return worldbuilding::make_id("img"); },
        [] { return worldbuilding::now_iso_utc(); },
        uploads_dir);
    wb_handler_->set_image_service(image_service);
}

} // namespace merak::app
