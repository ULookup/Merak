#include <merak/agent_loop.hpp>
#include <merak/anthropic_provider.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/config_loader.hpp>
#include <merak/http_server.hpp>
#include <merak/mcp_client.hpp>
#include <merak/openai_provider.hpp>
#include <merak/portable_pg.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/worldbuilding_tools.hpp>
#include <merak/worldbuilding_http_handler.hpp>
#include <merak/tool_catalog.hpp>
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
#include "client/runtime_client.hpp"
#include "commands/worldbuilding_commands.hpp"
#include "tui/screen_manager.hpp"
#include "tui/history_cell/welcome_cell.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
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
  "memory": {"enabled":false}
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

static int run_server(int argc,char**argv) {
    auto cfg=load_config();
    // --- Portable PostgreSQL ---
    std::unique_ptr<PortablePg> portable_pg;
    {
        auto exe_dir = std::filesystem::canonical(
            std::filesystem::path(argv[0]).parent_path());
        auto pg_path = exe_dir / "pg";
        if (std::filesystem::exists(pg_path) && cfg.memory.db_connection.empty()) {
            portable_pg = std::make_unique<PortablePg>(pg_path);
            if (portable_pg->start()) {
                cfg.memory.db_connection = portable_pg->connection_string();
                std::cout << "Portable PostgreSQL started on port " << portable_pg->port() << "\n";
            } else {
                std::cerr << "Warning: portable PostgreSQL failed to start\n";
                portable_pg.reset();
            }
        }
    }
    // Instantiate WorldbuildingService
    std::shared_ptr<worldbuilding::WorldbuildingService> wb_service;
    try {
        if (!cfg.memory.db_connection.empty()) {
            wb_service = std::make_shared<worldbuilding::WorldbuildingService>(
                cfg.memory.db_connection, merak_home() / "worldbuilding");
            wb_service->initialize();
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: WorldbuildingService not available: " << e.what() << "\n";
    }
    std::shared_ptr<LlmProvider>llm=cfg.llm.provider=="anthropic"
        ?std::static_pointer_cast<LlmProvider>(std::make_shared<AnthropicProvider>(cfg.llm))
        :std::static_pointer_cast<LlmProvider>(std::make_shared<OpenAIProvider>(cfg.llm));
    auto tools=std::make_shared<ToolRegistry>();tools->register_tool(std::make_unique<tools::ReadFileTool>());tools->register_tool(std::make_unique<tools::WriteFileTool>());tools->register_tool(std::make_unique<tools::StrReplaceTool>());tools->register_tool(std::make_unique<tools::MultiEditTool>());tools->register_tool(std::make_unique<tools::DeleteFileTool>());tools->register_tool(std::make_unique<tools::ListDirTool>());tools->register_tool(std::make_unique<tools::GlobTool>());tools->register_tool(std::make_unique<tools::GrepTool>());tools->register_tool(std::make_unique<tools::BashTool>());tools->set_permission_mode(cfg.agent.permission_mode);
    // Pinned meta-tool: always available for tool discovery
    tools->register_tool(std::make_unique<tools::ToolSearchTool>(tools));
    // Deferred platform tools (12)
    tools->register_tool(std::make_unique<tools::GitTool>());
    tools->register_tool(std::make_unique<tools::WebFetchTool>());
    tools->register_tool(std::make_unique<tools::WebSearchTool>());
    tools->register_tool(std::make_unique<tools::LspTool>());
    tools->register_tool(std::make_unique<tools::SymbolsTool>());
    tools->register_tool(std::make_unique<tools::MemoryTool>());
    tools->register_tool(std::make_unique<tools::SessionTool>());
    tools->register_tool(std::make_unique<tools::AgentTool>());
    tools->register_tool(std::make_unique<tools::TaskTool>());
    tools->register_tool(std::make_unique<tools::AskUserTool>());
    tools->register_tool(std::make_unique<tools::EnterPlanModeTool>());
    tools->register_tool(std::make_unique<tools::ExitPlanModeTool>());
    // Set default platform capabilities (empty = all non-gated tools visible)
    tools->set_capabilities(CapabilitySet::platform_default());
    // Register Worldbuilding tools if service is available
    if (wb_service) {
        tools->set_capabilities(tools->capabilities() | Capability::Worldbuilding);
        worldbuilding::WorldbuildingTools wb_tools(*wb_service);
        auto wb_ctx = worldbuilding::ToolContext{};
        auto god_tools = wb_tools.create_tools(worldbuilding::AgentKind::God, wb_ctx);
        for (auto& tool : god_tools) {
            tools->register_tool(std::move(tool));
        }
    }
    std::vector<std::shared_ptr<McpClient>>mcp;std::vector<McpServerStatus>mcp_status;
    for(const auto& mc:cfg.mcp_servers){if(!mc.enabled)continue;auto c=std::make_shared<McpClient>(mc);auto connected=c->connect();mcp_status.push_back({mc.name,connected.has_value()});if(connected){tools->import_from_mcp(c).get();mcp.push_back(c);}}
    auto memory_cfg=cfg.memory;if(memory_cfg.db_connection.empty())memory_cfg.enabled=false;auto memory=std::make_shared<MemoryStore>(memory_cfg,nullptr);
    if(memory_cfg.enabled)memory->init_db();
    auto make_context=[cfg](std::shared_ptr<LlmProvider> provider){
        auto counter=std::make_shared<TokenCounter>();
        TokenBudget budget{128000,cfg.agent.reserve_ratio,cfg.agent.memory_budget_ratio};
        auto context=std::make_shared<ContextAssembler>(budget,counter);
        auto compactor=std::make_shared<Compactor>(provider,counter);
        return std::pair{context,compactor};
    };
    auto [context,compactor]=make_context(llm);
    auto factory=[cfg,llm,tools,memory,context,compactor](const std::string&model){AgentLoop::Config c;c.system_prompt=cfg.agent.system_prompt;c.max_turns=cfg.agent.max_tool_turns;c.default_model=model.empty()?cfg.llm.default_model:model;c.max_output_tokens=cfg.llm.max_output_tokens;return std::make_unique<AgentLoop>(c,llm,tools,memory,context,compactor);};
    auto sub_executor=[cfg,llm,tools,memory,make_context](const SubAgentConfig&agent,const std::string&task,RunControl&control){
        auto sub_tools=std::make_shared<ToolRegistry>();sub_tools->set_permission_mode(cfg.agent.permission_mode);
        if(agent.tool_allowlist.empty()){for(const auto&spec:tools->all_tools()){if(auto*tool=tools->get_tool(spec.name))sub_tools->register_tool(tool->clone());}}
        else{for(const auto&name:agent.tool_allowlist){if(auto*tool=tools->get_tool(name))sub_tools->register_tool(tool->clone());}}
        auto [sub_context,sub_compactor]=make_context(llm);
        AgentLoop::Config c;c.system_prompt=agent.system_prompt.empty()?cfg.agent.system_prompt:agent.system_prompt;c.max_turns=cfg.agent.max_tool_turns;c.default_model=agent.model.empty()?cfg.llm.default_model:agent.model;c.max_output_tokens=cfg.llm.max_output_tokens;
        auto loop=std::make_unique<AgentLoop>(c,llm,sub_tools,memory,sub_context,sub_compactor);
        return loop->run(task,control).get();
    };
    auto runtime=std::make_shared<RuntimeService>(merak_home(),factory,cfg.agent.sub_agents,sub_executor);runtime->initialize();
    RuntimeMetadata metadata;
    metadata.provider = cfg.llm.provider;
    metadata.model = cfg.llm.default_model;
    metadata.models = cfg.models;
    metadata.permission_mode = cfg.agent.permission_mode;
    metadata.memory_enabled = memory_cfg.enabled;
    metadata.tools = tools->all_tools();
    metadata.mcp_servers = mcp_status;
    metadata.agents = runtime->agents();
    HttpServer server(runtime, metadata, merak_home());
    // Serve static WebUI files
    {
        auto exe_dir = std::filesystem::canonical(
            std::filesystem::path(argv[0]).parent_path());
        auto webui_path = exe_dir / "webui";
        if (std::filesystem::exists(webui_path)) {
            server.serve_static_dir("/", webui_path.string());
            std::cout << "Serving WebUI from " << webui_path << "\n";
        }
    }
    // Register Worldbuilding HTTP routes
    if (wb_service) {
        auto wb_handler = std::make_shared<WorldbuildingHttpHandler>(wb_service);
        wb_handler->install_routes(server.raw_server());
    }
    auto port=parse_port(argc,argv);std::cout<<"merak serve listening on 127.0.0.1:"<<port<<"\n";server.listen(port);return 0;
}

static ToolCall tool_call(const nlohmann::json&p){return{p.value("id",p.value("tool_call_id","")),p.value("name",p.value("tool","")),p.value("arguments","")};}
static ToolResult tool_result(const nlohmann::json&p){return{p.value("id",""),p.value("output",""),p.value("is_error",false)};}

static int run_tui(int argc,char**argv) {
    auto server=option(argc,argv,"--server","http://127.0.0.1:3888");client::RuntimeClient api(server);auto meta=api.metadata();
    tui::ScreenManager ui;ui.set_runtime_metadata(meta);ui.status_bar().set_provider(meta.value("provider","none"));ui.status_bar().set_model(meta.value("model","none"));ui.status_bar().set_cwd(std::filesystem::current_path().string());{auto branch=tui::ExternalEditorResolver::shell_output("git branch --show-current 2>/dev/null");ui.status_bar().set_git_branch(branch);}
    std::string session_id=option(argc,argv,"--session");if(session_id.empty())session_id=api.create_session()["session_id"];else api.session(session_id);
    std::mutex state_mutex;std::string current_run;long long last_seq=0;std::atomic<bool>stop_stream=false;
    std::thread stream;
    auto apply=[&](client::SseFrame frame,bool recovering=false){std::lock_guard lock(state_mutex);last_seq=std::max(last_seq,frame.seq);auto e=frame.payload;auto type=e.value("type",frame.type);auto p=e.value("payload",nlohmann::json::object());if(type=="run_started"){current_run=e.value("run_id","");if(recovering)ui.post([&ui,text=p.value("message","")]{ui.timeline().submit_user(text);});}else if(type=="text_delta")ui.post([&ui,text=p.value("text","")]{ui.timeline().append_assistant(text);});else if(type=="state_changed")ui.post([&ui,state=p.value("to","")]{ui.status_bar().set_state(state);});else if(type=="usage_updated")ui.post([&ui,p]{ui.record_usage(p.value("input_tokens",0),p.value("output_tokens",0),p.value("exact",false));});else if(type=="tool_started"){auto c=tool_call(p);ui.post([&ui,c]{ui.record_tool_start();ui.timeline().start_tool(c);});}else if(type=="tool_completed"){auto r=tool_result(p);ui.post([&ui,r]{ui.record_tool_end();ui.timeline().finish_tool(r);});}else if(type=="delegation_started"){ui.post([&ui,p]{auto agents=p.value("agent_ids",nlohmann::json::array());ui.timeline().add_system("Team "+p.value("pattern","")+" started with "+std::to_string(agents.size())+" agents");});}else if(type=="sub_run_started"){ui.post([&ui,p]{ui.record_agent_start();ui.timeline().add_system("Agent "+p.value("agent_id","")+" started");});}else if(type=="sub_run_completed"){ui.post([&ui,p]{ui.record_agent_end();ui.timeline().add_system("Agent "+p.value("agent_id","")+" "+p.value("status","completed"));});}else if(type=="delegation_completed"){ui.post([&ui,p]{ui.timeline().commit_active();auto output=p.value("aggregated_output","");if(!output.empty())ui.timeline().append_assistant(output);ui.timeline().commit_active();ui.timeline().add_system("Team completed · "+p.value("status","completed"));});}else if(type=="approval_requested"){auto c=tool_call(p);auto approval=p.value("approval_id","");ui.post([&ui,&api,c,approval]{ui.request_approval(c,[&api,approval](bool allow){api.resolve_approval(approval,allow);});});}else if(type=="run_completed"||type=="run_failed"||type=="run_cancelled"||type=="run_interrupted"){current_run.clear();ui.post([&ui,type,p]{ui.timeline().commit_active();if(type=="run_failed")ui.timeline().add_system(p.value("error","Run failed"),true);ui.status_bar().set_state(type=="run_cancelled"?"Cancelled":"Idle");ui.finish_remote_run();});}};
    auto start_stream=[&]{stop_stream=false;stream=std::thread([&]{while(!stop_stream){long long cursor;{std::lock_guard lock(state_mutex);cursor=last_seq;}api.stream_events(session_id,cursor,[&](auto f){apply(std::move(f));},stop_stream);if(!stop_stream)std::this_thread::sleep_for(std::chrono::milliseconds(300));}});};
    for(const auto&e:api.events(session_id)["events"])apply({e.value("seq",0LL),e.value("type",""),e},true);start_stream();
    ui.timeline().commit(std::make_shared<tui::WelcomeCell>(
        "0.1.0", meta.value("model", "none"), ui.status_bar().git_branch()));
    ui.timeline().add_system("Connected to merak serve · session "+session_id);
    ui.set_on_cancel([&]{std::string run;{std::lock_guard lock(state_mutex);run=current_run;}if(!run.empty())api.cancel_run(run);});
    ui.set_on_command([&](std::string input){if(input=="/exit"||input=="/quit"){ui.exit();return;}if(input=="/help"){ui.open_help();return;}
    // Worldbuilding commands
    if (input.rfind("/world", 0) == 0 ||
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
        input.rfind("@", 0) == 0) {
        auto wb_cmd = commands::parse_worldbuilding_command(input, "", "", "");
        if (wb_cmd) {
            auto result = commands::execute_worldbuilding_command(*wb_cmd,
                [&api](const std::string& method, const std::string& path,
                       const nlohmann::json& body) {
                    return api.request(method, path, body);
                });
            ui.timeline().add_system(result);
            return;
        }
    }
    if(input=="/context"){ui.open_context();return;}if(input=="/model"||input.rfind("/model ",0)==0){ui.open_model_selector();return;}if(input=="/transcript"){ui.open_transcript();return;}if(input=="/tool-calls"){ui.open_tool_browser();return;}if(input=="/agents"){std::ostringstream out;out<<"Agents";for(const auto&a:meta.value("agents",nlohmann::json::array()))out<<"\n"<<a.value("id","")<<"  "<<a.value("description","");ui.timeline().add_system(out.str());return;}if(input.rfind("/team ",0)==0){std::istringstream parts(input.substr(6));std::string pattern,agent_list;parts>>pattern>>agent_list;std::string task;std::getline(parts,task);while(!task.empty()&&std::isspace(static_cast<unsigned char>(task.front())))task.erase(task.begin());auto agents=split_csv(agent_list);pattern=normalize_team_pattern(pattern);if((pattern!="fan_out"&&pattern!="sequential"&&pattern!="pipeline")||agents.empty()||task.empty()){ui.timeline().add_system("Usage: /team fanout|sequential|pipeline agent1,agent2 task",true);return;}ui.timeline().submit_user(input);ui.start_background([&api,&ui,&session_id,pattern,agents,task]{try{api.start_delegation(session_id,pattern,agents,task);}catch(const std::exception&e){ui.post([&ui,error=std::string(e.what())]{ui.timeline().add_system(error,true);ui.finish_remote_run();});}});return;}if(input=="/tools"){ui.open_tools();return;}if(input=="/memory"||input.rfind("/memory ",0)==0){try{ui.set_memory_items(api.memory(session_id).value("items",nlohmann::json::array()));}catch(const std::exception&e){ui.timeline().add_system(std::string("Memory refresh failed: ")+e.what(),true);}ui.open_memory();return;}if(input=="/session list"){auto sessions=api.list_sessions()["sessions"];std::ostringstream out;out<<"Sessions";for(const auto&s:sessions)out<<"\n"<<s.value("id","")<<"  "<<s.value("title","");ui.timeline().add_system(out.str());return;}if(input=="/clear")input="/session new";if(input=="/session new"||input.rfind("/session use ",0)==0){if(ui.busy()||ui.queued_messages()>0){ui.timeline().add_system("Cannot switch sessions while a run or queued message is active",true);return;}stop_stream=true;if(stream.joinable())stream.join();session_id=input=="/session new"?api.create_session()["session_id"].get<std::string>():input.substr(13);api.session(session_id);{std::lock_guard lock(state_mutex);last_seq=0;current_run.clear();}ui.reset_timeline();for(const auto&e:api.events(session_id)["events"])apply({e.value("seq",0LL),e.value("type",""),e},true);start_stream();ui.timeline().add_system("Using session "+session_id);return;}if(input.starts_with("/")){ui.timeline().add_system("Unknown command: "+input,true);return;}auto model=ui.selected_model();ui.start_background([&api,&ui,&session_id,input=std::move(input),model]{try{api.start_run(session_id,input,model);}catch(const std::exception&e){ui.post([&ui,error=std::string(e.what())]{ui.timeline().add_system(error,true);ui.finish_remote_run();});}});});
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
