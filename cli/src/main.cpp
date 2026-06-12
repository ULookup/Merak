#include <merak/agent_loop.hpp>
#include <merak/anthropic_provider.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/config_loader.hpp>
#include <merak/http_server.hpp>
#include <merak/mcp_client.hpp>
#include <merak/openai_provider.hpp>
#include <merak/openai_embedding_provider.hpp>
#include <merak/portable_pg.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/worldbuilding_tools.hpp>
#include <merak/worldbuilding/pipeline_manager.hpp>
#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/kg/neo4j_provider.hpp>
#include <merak/storage/image_service.hpp>
#include <merak/storage/local_file_image_store.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <pqxx/pqxx>
#include <merak/worldbuilding_http_handler.hpp>
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
#include <merak/session_store.hpp>
#include <merak/edit_journal.hpp>
#include "client/runtime_client.hpp"
#include "tui/persistence/transcript.hpp"
#include "commands/worldbuilding_commands.hpp"
#include "tui/screen_manager.hpp"
#include "tui/history_cell/welcome_cell.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <regex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

using namespace merak;

static const char* SETTINGS_TEMPLATE = R"({
  "llm": {"provider":"openai","api_key":"sk-your-api-key-here","default_model":"gpt-4o","max_output_tokens":4096},
  "agent": {"system_prompt":"You are a helpful AI assistant. Use tools to complete tasks."},
  "memory": {"enabled":false},
  "knowledge_graph": {
    "enabled": false,
    "neo4j": {
      "uri": "bolt://localhost:7687",
      "user": "neo4j",
      "password": "",
      "database": "merak"
    }
  },
  "tui": {
    "theme": {
      "preset": "auto",
      "accent": "yellow",
      "selected_bg": 236,
      "selected_fg": 252
    }
  }
})";

static std::filesystem::path merak_home() {
    if (auto* home = std::getenv("HOME")) return std::filesystem::path(home) / ".merak";
    return ".merak";
}
static void show_help() {
    std::cout << "Merak Agent\n\n"
              << "  merak --init\n"
              << "  merak serve [--port 3888]\n"
              << "  merak tui [--server http://127.0.0.1:3888] [--session <id>]\n";
}
static int do_init() {
    auto dir=merak_home();std::filesystem::create_directories(dir);auto file=dir/"settings.local.json";
    if(std::filesystem::exists(file)){std::cerr<<"Already exists: "<<file<<"\n";return 1;}
    std::ofstream out(file);out<<SETTINGS_TEMPLATE;std::cout<<"Created "<<file<<"\n";return 0;
}
static Config load_config() {
    auto result=ConfigLoader::load();if(!result.has_value())throw std::runtime_error(result.error().what());
    auto cfg=result.value();if(cfg.llm.api_key.empty())throw std::runtime_error("Missing API key. Run merak --init.");return cfg;
}
static int parse_port(int argc,char**argv){for(int i=2;i+1<argc;++i)if(std::string(argv[i])=="--port")return std::stoi(argv[i+1]);return 3888;}
static std::string option(int argc,char**argv,const std::string&name,const std::string&fallback=""){for(int i=2;i+1<argc;++i)if(name==argv[i])return argv[i+1];return fallback;}
static std::vector<std::string> split_csv(std::string value){
    std::vector<std::string> out;std::stringstream stream(value);std::string item;
    while(std::getline(stream,item,',')){item.erase(item.begin(),std::find_if(item.begin(),item.end(),[](unsigned char c){return !std::isspace(c);}));item.erase(std::find_if(item.rbegin(),item.rend(),[](unsigned char c){return !std::isspace(c);}).base(),item.end());if(!item.empty())out.push_back(item);}
    return out;
}
static std::string normalize_team_pattern(std::string pattern){
    if(pattern=="fanout")return"fan_out";
    return pattern;
}
static nlohmann::json tui_theme_json(const TuiThemeConfig& theme) {
    nlohmann::json out;
    if (theme.preset_set) out["preset"] = theme.preset.empty() ? "auto" : theme.preset;
    for (const auto& [key, value] : theme.colors) out[key] = value;
    return out;
}

#ifdef _WIN32
#include <windows.h>
#endif

static std::filesystem::path exe_dir_path() {
#ifdef _WIN32
    WCHAR path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return {};
    std::filesystem::path p(path);
    return p.parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}

static int run_server(int argc,char**argv) {
    auto cfg=load_config();
    // --- Portable PostgreSQL ---
    std::unique_ptr<PortablePg> portable_pg;
    {
        auto exe = exe_dir_path();
        auto pg_path = exe / "pg";
        if (!exe.empty() && std::filesystem::exists(pg_path) && cfg.memory.db_connection.empty()) {
            portable_pg = std::make_unique<PortablePg>(pg_path);
            if (portable_pg->start()) {
                cfg.memory.db_connection = portable_pg->connection_string();
                cfg.memory.enabled = true;
                std::cout << "Portable PostgreSQL started on port " << portable_pg->port() << "\n";
            } else {
                std::cerr << "Warning: portable PostgreSQL failed to start\n";
                portable_pg.reset();
            }
        }
    }
    // Instantiate WorldbuildingService
    std::shared_ptr<worldbuilding::WorldbuildingService> wb_service;
    std::unique_ptr<merak::kg::KnowledgeGraphProvider> kg_provider;
    if (!cfg.memory.db_connection.empty()) {
        if (cfg.knowledge_graph.enabled) {
            try {
                kg_provider = std::make_unique<merak::kg::Neo4jKGProvider>(
                    cfg.knowledge_graph.neo4j_uri,
                    cfg.knowledge_graph.neo4j_user,
                    cfg.knowledge_graph.neo4j_password,
                    cfg.knowledge_graph.neo4j_database);
                std::cout << "Knowledge Graph: connected to Neo4j at "
                          << cfg.knowledge_graph.neo4j_uri << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Warning: Knowledge Graph unavailable: " << e.what() << "\n";
                kg_provider.reset();
            }
        }
        try {
            wb_service = std::make_shared<worldbuilding::WorldbuildingService>(
                cfg.memory.db_connection, merak_home() / "worldbuilding",
                std::move(kg_provider));
            wb_service->initialize();
            wb_service->set_diary_context_limit(cfg.memory.diary_context_limit);
        } catch (const std::exception& e) {
            std::cerr << "Warning: WorldbuildingService not available: " << e.what() << "\n";
        }
    }
    std::shared_ptr<LlmProvider>llm=cfg.llm.provider=="anthropic"
        ?std::static_pointer_cast<LlmProvider>(std::make_shared<AnthropicProvider>(cfg.llm))
        :std::static_pointer_cast<LlmProvider>(std::make_shared<OpenAIProvider>(cfg.llm));
    auto tools=std::make_shared<ToolRegistry>();
    tools->register_platform_basics();
    tools->register_tool(std::make_unique<tools::MultiEditTool>());
    tools->register_tool(std::make_unique<tools::DeleteFileTool>());
    tools->set_permission_mode(cfg.agent.permission_mode);

    // Pinned meta-tool: always available for tool discovery
    tools->register_tool(std::make_unique<tools::ToolSearchTool>(tools));

    // Deferred platform tools
    tools->register_tool(std::make_unique<tools::GitTool>());
    tools->register_tool(std::make_unique<tools::WebFetchTool>());
    tools->register_tool(std::make_unique<tools::WebSearchTool>());
    tools->register_tool(std::make_unique<tools::LspTool>());
    tools->register_tool(std::make_unique<tools::SymbolsTool>());
    tools->register_tool(std::make_unique<tools::TaskTool>());
    tools->register_tool(std::make_unique<tools::AskUserTool>());
    auto plan_mode = std::make_shared<std::atomic<bool>>(false);
    auto session_store = std::make_shared<SessionStore>(merak_home());
    tools->register_tool(std::make_unique<tools::EnterPlanModeTool>(plan_mode));
    tools->register_tool(std::make_unique<tools::ExitPlanModeTool>(plan_mode, session_store));

    // Register Worldbuilding tools if service is available
    if (wb_service) {
        worldbuilding::WorldbuildingTools wb_tools(
            *wb_service, llm, cfg.memory.diary_compression_threshold,
            cfg.memory.diary_model);
        auto god_tools = wb_tools.create_tools(worldbuilding::AgentKind::God);
        tools->register_all(std::move(god_tools));
    }
    std::vector<std::shared_ptr<McpClient>>mcp;std::vector<McpServerStatus>mcp_status;
    for(const auto& mc:cfg.mcp_servers){if(!mc.enabled)continue;auto c=std::make_shared<McpClient>(mc);auto connected=c->connect();mcp_status.push_back({mc.name,connected.has_value()});if(connected){tools->import_from_mcp(c).get();mcp.push_back(c);}}
    auto memory_cfg=cfg.memory;if(memory_cfg.db_connection.empty())memory_cfg.enabled=false;
std::shared_ptr<EmbeddingProvider> embedder;
if(memory_cfg.enabled&&!memory_cfg.embedding_api_key.empty()){
    OpenAIEmbeddingProvider::Config embed_cfg;
    embed_cfg.api_url=memory_cfg.embedding_api_url;
    embed_cfg.api_key=memory_cfg.embedding_api_key;
    embed_cfg.model=memory_cfg.embedding_model;
    embed_cfg.cache_size=memory_cfg.embedding_cache_size;
    embed_cfg.batch_size=memory_cfg.embedding_batch_size;
    embed_cfg.timeout_ms=memory_cfg.embedding_timeout_ms;
    embedder=std::make_shared<OpenAIEmbeddingProvider>(embed_cfg);
    std::cout<<"Embedding: using "<<memory_cfg.embedding_model<<" via "<<memory_cfg.embedding_api_url<<"\n";
}else if(memory_cfg.enabled&&memory_cfg.embedding_api_key.empty()){
    std::cerr<<"Warning: Memory enabled but embedding_api_key not set, semantic search disabled\n";
}
auto memory=std::make_shared<MemoryStore>(memory_cfg,embedder);
    if(memory_cfg.enabled)memory->init_db();
    auto make_context=[cfg](std::shared_ptr<LlmProvider> provider){
        auto counter=std::make_shared<TokenCounter>();
        TokenBudget budget{128000,cfg.agent.reserve_ratio,cfg.agent.memory_budget_ratio};
        auto context=std::make_shared<ContextAssembler>(budget,counter);
        auto compactor=std::make_shared<Compactor>(provider,counter);
        return std::pair{context,compactor};
    };
    auto [context,compactor]=make_context(llm);
    // Register MemoryTool and SessionTool now that memory store is available
    tools->register_tool(std::make_unique<tools::MemoryTool>(memory));
    auto edit_journal = std::make_shared<EditJournal>();
    tools->register_tool(std::make_unique<tools::SessionTool>(memory, compactor, edit_journal.get()));
    auto factory=[cfg,llm,tools,memory,context,compactor,plan_mode](const std::string&model){AgentLoop::Config c;c.system_prompt=cfg.agent.system_prompt;c.max_turns=cfg.agent.max_tool_turns;c.default_model=model.empty()?cfg.llm.default_model:model;c.max_output_tokens=cfg.llm.max_output_tokens;auto loop=std::make_unique<AgentLoop>(c,llm,tools,memory,context,compactor);loop->set_plan_mode_source(plan_mode);return loop;};
    auto sub_executor=[cfg,llm,tools,memory,make_context,plan_mode](const SubAgentConfig&agent,const std::string&task,RunControl&control){
        auto sub_tools=std::make_shared<ToolRegistry>();sub_tools->set_permission_mode(cfg.agent.permission_mode);
        if(agent.tool_allowlist.empty()){for(const auto&spec:tools->all_tools()){if(auto*tool=tools->get_tool(spec.name))sub_tools->register_tool(tool->clone());}}
        else{for(const auto&name:agent.tool_allowlist){if(auto*tool=tools->get_tool(name))sub_tools->register_tool(tool->clone());}}
        auto [sub_context,sub_compactor]=make_context(llm);
        AgentLoop::Config c;c.system_prompt=agent.system_prompt.empty()?cfg.agent.system_prompt:agent.system_prompt;c.max_turns=cfg.agent.max_tool_turns;c.default_model=agent.model.empty()?cfg.llm.default_model:agent.model;c.max_output_tokens=cfg.llm.max_output_tokens;
        auto loop=std::make_unique<AgentLoop>(c,llm,sub_tools,memory,sub_context,sub_compactor);
        loop->set_plan_mode_source(plan_mode);
        return loop->run(task,control).get();
    };
    tools->register_tool(std::make_unique<tools::AgentTool>(cfg.agent.sub_agents, sub_executor));
    auto runtime=std::make_shared<RuntimeService>(merak_home(),factory,cfg.agent.sub_agents,sub_executor);runtime->initialize();
    if (wb_service) runtime->set_worldbuilding_service(wb_service.get());
    // Initialize PipelineManager
    std::shared_ptr<merak::worldbuilding::PipelineManager> pipeline_mgr;
    if (wb_service && !cfg.memory.db_connection.empty()) {
        auto condition_evaluator = merak::worldbuilding::ConditionEvaluator::create_default();
        if (wb_service->kg_provider()) {
            condition_evaluator->set_kg_provider(wb_service->kg_provider());
        }
        pipeline_mgr = std::make_shared<merak::worldbuilding::PipelineManager>(
            merak::worldbuilding::PipelineManager::Dependencies{
                .pg_connection_factory = [db_connection = cfg.memory.db_connection]() -> std::shared_ptr<pqxx::connection> {
                    return std::make_shared<pqxx::connection>(db_connection);
                },
                .event_emitter = [&runtime](const merak::RuntimeEvent& e) {
                    auto wid = e.payload.value("world_id", "");
                    if (!wid.empty()) {
                        runtime->broadcast_to_world(wid, e);
                    } else if (!e.session_id.empty()) {
                        runtime->emit_event(e.session_id, e.run_id, e.type, e.payload);
                    }
                },
                .pipeline_config_dir = [] {
                    auto exe = exe_dir_path();
                    auto primary = exe / "pipelines";
                    if (!exe.empty() && std::filesystem::exists(primary)) return primary;
                    return exe / ".." / "config" / "pipelines";
                }(),
                .condition_evaluator = condition_evaluator,
            }
        );
        pipeline_mgr->initialize();
        runtime->set_pipeline_manager(pipeline_mgr);
    }
    RuntimeMetadata metadata;
    metadata.provider = cfg.llm.provider;
    metadata.model = cfg.llm.default_model;
    metadata.models = cfg.models;
    metadata.permission_mode = cfg.agent.permission_mode;
    metadata.memory_enabled = memory_cfg.enabled;
    metadata.worldbuilding_enabled = wb_service != nullptr;
    metadata.tui_theme = tui_theme_json(cfg.tui.theme);
    metadata.tools = tools->all_tools();
    metadata.mcp_servers = mcp_status;
    metadata.agents = runtime->agents();
    HttpServer server(runtime, metadata, merak_home(), llm);
    // Serve static WebUI files
    {
        auto exe = exe_dir_path();
        auto webui_path = exe / "webui";
        if (!exe.empty() && std::filesystem::exists(webui_path)) {
            server.serve_static_dir("/", webui_path.string());
            std::cout << "Serving WebUI from " << webui_path << "\n";
        }
    }
    // Register Worldbuilding HTTP routes
    std::shared_ptr<WorldbuildingHttpHandler> wb_handler;
    if (wb_service) {
        wb_handler = std::make_shared<WorldbuildingHttpHandler>(wb_service, runtime);
        if (pipeline_mgr) wb_handler->set_pipeline_manager(pipeline_mgr);
        // Wire up ImageService for character image upload
        {
            auto images_dir = (merak_home() / "data" / "images").string();
            auto uploads_dir = (merak_home() / "data" / "uploads" / "chunks").string();
            fs::create_directories(images_dir);
            fs::create_directories(uploads_dir);

            auto image_store = std::make_shared<merak::LocalFileImageStore>(
                images_dir, "/api/worldbuilding/images");

            // Shared DB connection — reused across all callbacks, guarded by mutex
            auto conn = std::make_shared<pqxx::connection>(cfg.memory.db_connection);
            auto conn_mutex = std::make_shared<std::mutex>();

            // Translates @param_name placeholders to $1, $2, ... for pgxx parameterized queries.
            // NOTE: regex matches any @[word] token. The SQL strings passed to these callbacks
            // must not contain @-prefixed literals outside of parameter placeholders.
            auto translate_sql = [](const std::string& sql, const nlohmann::json& params,
                                     std::string& translated, pqxx::params& pq_params) {
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

                for (const auto& name : param_names) {
                    auto p = params.find(name);
                    if (p == params.end()) {
                        pq_params.append(std::string{});
                        continue;
                    }
                    const auto& val = *p;
                    switch (val.type()) {
                    case nlohmann::json::value_t::string:
                        pq_params.append(val.get<std::string>());
                        break;
                    case nlohmann::json::value_t::boolean:
                        pq_params.append(val.get<bool>());
                        break;
                    case nlohmann::json::value_t::number_integer:
                    case nlohmann::json::value_t::number_unsigned:
                        pq_params.append(val.get<long long>());
                        break;
                    case nlohmann::json::value_t::number_float:
                        pq_params.append(val.get<double>());
                        break;
                    default:
                        pq_params.append(std::string{});
                        break;
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
                for (const auto& row : res) {
                    nlohmann::json obj;
                    for (int i = 0; i < static_cast<int>(row.columns()); ++i) {
                        if (row[i].is_null()) {
                            obj[row.column_name(i)] = nullptr;
                            continue;
                        }
                        // Use column OID to preserve the correct JSON type
                        auto oid = row.column_type(i);
                        switch (oid) {
                        case 16:   // bool
                            obj[row.column_name(i)] = row[i].as<bool>();
                            break;
                        case 20:   // int8
                        case 21:   // int2
                        case 23:   // int4
                            obj[row.column_name(i)] = row[i].as<long long>();
                            break;
                        case 700:  // float4
                        case 701:  // float8
                        case 1700: // numeric
                            obj[row.column_name(i)] = row[i].as<double>();
                            break;
                        default:
                            obj[row.column_name(i)] = row[i].c_str();
                            break;
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
                return true; // false on error would be silently ignored; let pqxx throw instead
            };

            auto image_service = std::make_shared<merak::ImageService>(
                image_store, db_query, db_exec,
                []{ return merak::worldbuilding::make_id("img"); },
                []{ return merak::worldbuilding::now_iso_utc(); },
                uploads_dir);
            wb_handler->set_image_service(image_service);
        }
        wb_handler->install_routes(server.raw_server());
    }
    auto port=parse_port(argc,argv);std::cout<<"merak serve listening on 127.0.0.1:"<<port<<"\n";server.listen(port);return 0;
}

static ToolCall tool_call(const nlohmann::json&p){return{p.value("id",p.value("tool_call_id","")),p.value("name",p.value("tool","")),p.value("arguments","")};}
static ToolResult tool_result(const nlohmann::json&p){return{p.value("id",""),p.value("output",""),p.value("is_error",false)};}

static bool is_worldbuilding_input(const std::string& input) {
    return input.rfind("/world", 0) == 0 ||
        input.rfind("/agent", 0) == 0 ||
        input.rfind("/story", 0) == 0 ||
        input.rfind("/chapter", 0) == 0 ||
        input.rfind("/arc", 0) == 0 ||
        input.rfind("/scene", 0) == 0 ||
        input.rfind("/time", 0) == 0 ||
        input.rfind("/foreshadow", 0) == 0 ||
        input.rfind("/secret", 0) == 0 ||
        input.rfind("/voice", 0) == 0 ||
        input.rfind("/memory", 0) == 0 ||
        input.rfind("/diary", 0) == 0 ||
        input.rfind("@", 0) == 0;
}

static bool command_requires_world(commands::WorldbuildingAction action) {
    using A = commands::WorldbuildingAction;
    switch (action) {
    case A::WorldList:
    case A::WorldCreate:
    case A::WorldUse:
    case A::WorldDelete:
    case A::None:
        return false;
    default:
        return true;
    }
}

static bool is_context_switch(commands::WorldbuildingAction action) {
    using A = commands::WorldbuildingAction;
    return action == A::WorldUse || action == A::ChapterUse || action == A::SceneUse;
}

static int run_tui(int argc,char**argv) {
    auto server=option(argc,argv,"--server","http://127.0.0.1:3888");client::RuntimeClient api(server);auto meta=api.metadata();
    theme::configure_theme(theme::load_theme_from_metadata(meta.value("tui", nlohmann::json::object()).value("theme", nlohmann::json::object())));
    tui::ScreenManager ui;ui.set_runtime_metadata(meta);ui.status_bar().set_provider(meta.value("provider","none"));ui.status_bar().set_model(meta.value("model","none"));ui.status_bar().set_cwd(std::filesystem::current_path().string());{auto branch=tui::ExternalEditorResolver::shell_output("git branch --show-current 2>/dev/null");ui.status_bar().set_git_branch(branch);}
    std::string session_id=option(argc,argv,"--session");if(session_id.empty())session_id=api.create_session()["session_id"];else api.session(session_id);
    {auto sj=api.session(session_id);tui::persistence::SessionMeta m;m.session_id=session_id;m.title=sj.value("title","");m.created_at=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());m.model=meta.value("model","none");m.cwd=std::filesystem::current_path().string();tui::persistence::update_index(session_id,m);}
    std::mutex state_mutex;std::string current_run;long long last_seq=0;std::atomic<bool>stop_stream=false;
    std::string current_world_id,current_chapter_id,current_scene_id;
    std::thread stream;
    auto apply=[&](client::SseFrame frame,bool recovering=false){std::lock_guard lock(state_mutex);last_seq=std::max(last_seq,frame.seq);auto e=frame.payload;auto type=e.value("type",frame.type);auto p=e.value("payload",nlohmann::json::object());if(type=="run_started"){current_run=e.value("run_id","");if(recovering)ui.post([&ui,text=p.value("message","")]{ui.timeline().submit_user(text);});}else if(type=="text_delta")ui.post([&ui,text=p.value("text","")]{ui.timeline().append_assistant(text);});else if(type=="state_changed")ui.post([&ui,state=p.value("to","")]{ui.status_bar().set_state(state);});else if(type=="usage_updated")ui.post([&ui,p]{ui.record_usage(p.value("input_tokens",0),p.value("output_tokens",0),p.value("exact",false));});else if(type=="tool_started"){auto c=tool_call(p);ui.post([&ui,c]{ui.record_tool_start();ui.timeline().start_tool(c);});}else if(type=="tool_completed"){auto r=tool_result(p);ui.post([&ui,r]{ui.record_tool_end();ui.timeline().finish_tool(r);});}else if(type=="delegation_started"){ui.post([&ui,p]{auto agents=p.value("agent_ids",nlohmann::json::array());ui.timeline().add_system("Team "+p.value("pattern","")+" started with "+std::to_string(agents.size())+" agents");});}else if(type=="sub_run_started"){ui.post([&ui,p]{ui.record_agent_start();ui.timeline().add_system("Agent "+p.value("agent_id","")+" started");});}else if(type=="sub_run_completed"){ui.post([&ui,p]{ui.record_agent_end();ui.timeline().add_system("Agent "+p.value("agent_id","")+" "+p.value("status","completed"));});}else if(type=="delegation_completed"){ui.post([&ui,p]{ui.timeline().commit_active();auto output=p.value("aggregated_output","");if(!output.empty())ui.timeline().append_assistant(output);ui.timeline().commit_active();ui.timeline().add_system("Team completed · "+p.value("status","completed"));});}else if(type=="approval_requested"){auto c=tool_call(p);auto approval=p.value("approval_id","");ui.post([&ui,&api,c,approval]{ui.request_approval(c,[&api,approval](bool allow){api.resolve_approval(approval,allow);});});}else if(type=="run_completed"||type=="run_failed"||type=="run_cancelled"||type=="run_interrupted"){current_run.clear();ui.post([&ui,type,p]{ui.timeline().commit_active();if(type=="run_failed")ui.timeline().add_system(p.value("error","Run failed"),true);ui.status_bar().set_state(type=="run_cancelled"?"Cancelled":"Idle");ui.finish_remote_run();});}};
    auto start_stream=[&]{stop_stream=false;stream=std::thread([&]{while(!stop_stream){long long cursor;{std::lock_guard lock(state_mutex);cursor=last_seq;}api.stream_events(session_id,cursor,[&](auto f){apply(std::move(f));},stop_stream);if(!stop_stream)std::this_thread::sleep_for(std::chrono::milliseconds(300));}});};
    for(const auto&e:api.events(session_id)["events"])apply({e.value("seq",0LL),e.value("type",""),e},true);start_stream();
    ui.timeline().commit(std::make_shared<tui::WelcomeCell>(
        "0.1.0", meta.value("model", "none"), ui.status_bar().git_branch()));
    ui.timeline().add_system("Connected to merak serve · session "+session_id);
    ui.set_on_cancel([&]{std::string run;{std::lock_guard lock(state_mutex);run=current_run;}if(!run.empty())api.cancel_run(run);});
    ui.set_on_command([&](std::string input){if(input=="/exit"||input=="/quit"){ui.exit();return;}if(input=="/help"){ui.open_help();return;}
    if (is_worldbuilding_input(input)) {
        auto wb_cmd = commands::parse_worldbuilding_command(
            input, current_world_id, current_chapter_id, current_scene_id);
        if (wb_cmd && wb_cmd->action != commands::WorldbuildingAction::None) {
            const auto worldbuilding_meta = meta.value("worldbuilding", nlohmann::json::object());
            const bool worldbuilding_enabled = worldbuilding_meta.value("enabled", false);
            if (is_context_switch(wb_cmd->action)) {
                if (wb_cmd->args.empty() || wb_cmd->args[0].empty()) {
                    ui.timeline().add_system("Usage: select an id, for example /world use <id>", true);
                    return;
                }
                if (wb_cmd->action == commands::WorldbuildingAction::WorldUse) {
                    current_world_id = wb_cmd->args[0];
                    current_chapter_id.clear();
                    current_scene_id.clear();
                    ui.timeline().add_system("Using world " + current_world_id);
                    return;
                }
                if (wb_cmd->action == commands::WorldbuildingAction::ChapterUse) {
                    if (current_world_id.empty()) {
                        ui.timeline().add_system("Select a world first with /world use <id>", true);
                        return;
                    }
                    current_chapter_id = wb_cmd->args[0];
                    current_scene_id.clear();
                    ui.timeline().add_system("Using chapter " + current_chapter_id);
                    return;
                }
                if (wb_cmd->action == commands::WorldbuildingAction::SceneUse) {
                    if (current_world_id.empty()) {
                        ui.timeline().add_system("Select a world first with /world use <id>", true);
                        return;
                    }
                    current_scene_id = wb_cmd->args[0];
                    ui.timeline().add_system("Using scene " + current_scene_id);
                    return;
                }
            }
            if (command_requires_world(wb_cmd->action) && current_world_id.empty()) {
                ui.timeline().add_system("Select a world first with /world use <id>", true);
                return;
            }
            if (!worldbuilding_enabled) {
                ui.timeline().add_system(
                    "Worldbuilding API is not available. Enable memory.db_connection or start merak serve with bundled PostgreSQL.",
                    true);
                return;
            }
            auto result = commands::execute_worldbuilding_command(*wb_cmd,
                [&api](const std::string& method, const std::string& path,
                       const nlohmann::json& body) {
                    return api.request(method, path, body);
                });
            const auto formatted = tui::SystemCell::format_worldbuilding_result(result);
            ui.timeline().add_system(formatted, formatted.rfind("Error:", 0) == 0);
            return;
        }
    }
    if(input=="/context"){ui.open_context();return;}if(input=="/model"||input.rfind("/model ",0)==0){ui.open_model_selector();return;}if(input=="/transcript"){ui.open_transcript();return;}if(input=="/tool-calls"){ui.open_tool_browser();return;}if(input=="/agents"){std::ostringstream out;out<<"Agents";for(const auto&a:meta.value("agents",nlohmann::json::array()))out<<"\n"<<a.value("id","")<<"  "<<a.value("description","");ui.timeline().add_system(out.str());return;}if(input.rfind("/team ",0)==0){std::istringstream parts(input.substr(6));std::string pattern,agent_list;parts>>pattern>>agent_list;std::string task;std::getline(parts,task);while(!task.empty()&&std::isspace(static_cast<unsigned char>(task.front())))task.erase(task.begin());auto agents=split_csv(agent_list);pattern=normalize_team_pattern(pattern);if((pattern!="fan_out"&&pattern!="sequential"&&pattern!="pipeline")||agents.empty()||task.empty()){ui.timeline().add_system("Usage: /team fanout|sequential|pipeline agent1,agent2 task",true);return;}ui.timeline().submit_user(input);ui.start_background([&api,&ui,&session_id,pattern,agents,task]{try{api.start_delegation(session_id,pattern,agents,task);}catch(const std::exception&e){ui.post([&ui,error=std::string(e.what())]{ui.timeline().add_system(error,true);ui.finish_remote_run();});}});return;}if(input=="/tools"){ui.open_tools();return;}if(input=="/memory"||input.rfind("/memory ",0)==0){try{ui.set_memory_items(api.memory(session_id).value("items",nlohmann::json::array()));}catch(const std::exception&e){ui.timeline().add_system(std::string("Memory refresh failed: ")+e.what(),true);}ui.open_memory();return;}if(input=="/session list"){auto sessions=api.list_sessions()["sessions"];std::ostringstream out;out<<"Sessions";for(const auto&s:sessions){std::string title=s.value("title","");std::string id=s["id"];out<<"\n"<<(title.empty()?"New Session":title)<<"  ["<<id<<"]";}ui.timeline().add_system(out.str());return;}if(input.rfind("/session rename ",0)==0){std::string new_title=input.substr(16);while(!new_title.empty()&&std::isspace(static_cast<unsigned char>(new_title.front())))new_title.erase(new_title.begin());if(new_title.empty()){ui.timeline().add_system("Usage: /session rename <new title>",true);}else{api.request("PATCH","/v1/sessions/"+session_id,{{"title",new_title}});ui.timeline().add_system("Session renamed to: "+new_title);}return;}if(input=="/clear")input="/session new";if(input.rfind("/session new",0)==0||input.rfind("/session use ",0)==0){std::string title;if(input.rfind("/session new",0)==0){auto pos=input.find("--title");if(pos!=std::string::npos)title=input.substr(pos+8);while(!title.empty()&&std::isspace(static_cast<unsigned char>(title.front())))title.erase(title.begin());}if(ui.busy()||ui.queued_messages()>0){ui.timeline().add_system("Cannot switch sessions while a run or queued message is active",true);return;}stop_stream=true;if(stream.joinable())stream.join();if(input.rfind("/session new",0)==0){auto sj=api.create_session(title);session_id=sj["session_id"];tui::persistence::SessionMeta m;m.session_id=session_id;m.title=sj.value("session",nlohmann::json::object()).value("title","");m.created_at=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());m.model=meta.value("model","none");m.cwd=std::filesystem::current_path().string();tui::persistence::update_index(session_id,m);}else{session_id=input.substr(13);auto sj=api.session(session_id);tui::persistence::SessionMeta m;m.session_id=session_id;m.title=sj.value("title","");m.created_at=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());m.model=meta.value("model","none");m.cwd=std::filesystem::current_path().string();tui::persistence::update_index(session_id,m);}{std::lock_guard lock(state_mutex);last_seq=0;current_run.clear();}ui.reset_timeline();for(const auto&e:api.events(session_id)["events"])apply({e.value("seq",0LL),e.value("type",""),e},true);start_stream();ui.timeline().add_system("Using session "+session_id);return;}if(input.starts_with("/")){ui.timeline().add_system("Unknown command: "+input,true);return;}auto model=ui.selected_model();ui.start_background([&api,&ui,&session_id,input=std::move(input),model]{try{api.start_run(session_id,input,model);}catch(const std::exception&e){ui.post([&ui,error=std::string(e.what())]{ui.timeline().add_system(error,true);ui.finish_remote_run();});}});});
    ui.run();stop_stream=true;if(stream.joinable())stream.join();return 0;
}

int main(int argc,char**argv) {
    try{
        if(argc<2){show_help();return 1;}std::string command=argv[1];
        if(command=="--help"||command=="-h"){show_help();return 0;}
        if(command=="--init")return do_init();
        if(command=="serve")return run_server(argc,argv);
        if(command=="tui")return run_tui(argc,argv);
        show_help();return 1;
    }catch(const std::exception&e){std::cerr<<"Error: "<<e.what()<<"\n";return 1;}
}
