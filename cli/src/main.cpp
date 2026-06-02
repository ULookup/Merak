#include <merak/agent_loop.hpp>
#include <merak/anthropic_provider.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/config_loader.hpp>
#include <merak/http_server.hpp>
#include <merak/mcp_client.hpp>
#include <merak/openai_provider.hpp>
#include "client/runtime_client.hpp"
#include "tui/screen_manager.hpp"
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

static int run_server(int argc,char**argv) {
    auto cfg=load_config();std::shared_ptr<LlmProvider>llm=cfg.llm.provider=="anthropic"
        ?std::static_pointer_cast<LlmProvider>(std::make_shared<AnthropicProvider>(cfg.llm))
        :std::static_pointer_cast<LlmProvider>(std::make_shared<OpenAIProvider>(cfg.llm));
    auto tools=std::make_shared<ToolRegistry>();tools->register_tool(std::make_unique<tools::ReadFileTool>());tools->register_tool(std::make_unique<tools::WriteFileTool>());tools->register_tool(std::make_unique<tools::EditFileTool>());tools->register_tool(std::make_unique<tools::GlobTool>());tools->register_tool(std::make_unique<tools::GrepTool>());tools->register_tool(std::make_unique<tools::BashTool>());tools->set_permission_mode(cfg.agent.permission_mode);
    std::vector<std::shared_ptr<McpClient>>mcp;std::vector<McpServerStatus>mcp_status;
    for(const auto& mc:cfg.mcp_servers){if(!mc.enabled)continue;auto c=std::make_shared<McpClient>(mc);auto connected=c->connect();mcp_status.push_back({mc.name,connected.has_value()});if(connected){tools->import_from_mcp(c).get();mcp.push_back(c);}}
    auto memory_cfg=cfg.memory;if(memory_cfg.db_connection.empty())memory_cfg.enabled=false;auto memory=std::make_shared<MemoryStore>(memory_cfg,nullptr);
    if(memory_cfg.enabled)memory->init_db();
    auto counter=std::make_shared<TokenCounter>();TokenBudget budget{128000,cfg.agent.reserve_ratio,cfg.agent.memory_budget_ratio};auto context=std::make_shared<ContextAssembler>(budget,counter);auto compactor=std::make_shared<Compactor>(llm,counter);
    auto factory=[cfg,llm,tools,memory,context,compactor]{AgentLoop::Config c;c.system_prompt=cfg.agent.system_prompt;c.max_turns=cfg.agent.max_tool_turns;c.default_model=cfg.llm.default_model;c.max_output_tokens=cfg.llm.max_output_tokens;return std::make_unique<AgentLoop>(c,llm,tools,memory,context,compactor);};
    auto runtime=std::make_shared<RuntimeService>(merak_home(),factory);runtime->initialize();
    HttpServer server(runtime,{cfg.llm.provider,cfg.llm.default_model,tools->all_tools(),mcp_status});
    auto port=parse_port(argc,argv);std::cout<<"merak serve listening on 127.0.0.1:"<<port<<"\n";server.listen(port);return 0;
}

static ToolCall tool_call(const nlohmann::json&p){return{p.value("id",p.value("tool_call_id","")),p.value("name",p.value("tool","")),p.value("arguments","")};}
static ToolResult tool_result(const nlohmann::json&p){return{p.value("id",""),p.value("output",""),p.value("is_error",false)};}

static int run_tui(int argc,char**argv) {
    auto server=option(argc,argv,"--server","http://127.0.0.1:3888");client::RuntimeClient api(server);auto meta=api.metadata();
    tui::ScreenManager ui;ui.status_bar().set_provider(meta.value("provider","none"));ui.status_bar().set_model(meta.value("model","none"));
    std::string session_id=option(argc,argv,"--session");if(session_id.empty())session_id=api.create_session()["session_id"];else api.session(session_id);
    std::mutex state_mutex;std::string current_run;long long last_seq=0;std::atomic<bool>stop_stream=false;
    std::thread stream;
    auto apply=[&](client::SseFrame frame,bool recovering=false){std::lock_guard lock(state_mutex);last_seq=std::max(last_seq,frame.seq);auto e=frame.payload;auto type=e.value("type",frame.type);auto p=e.value("payload",nlohmann::json::object());if(type=="run_started"){current_run=e.value("run_id","");if(recovering)ui.post([&ui,text=p.value("message","")]{ui.timeline().submit_user(text);});}else if(type=="text_delta")ui.post([&ui,text=p.value("text","")]{ui.timeline().append_assistant(text);});else if(type=="state_changed")ui.post([&ui,state=p.value("to","")]{ui.status_bar().set_state(state);});else if(type=="usage_updated")ui.post([&ui,p]{ui.record_usage(p.value("input_tokens",0),p.value("output_tokens",0),p.value("exact",false));});else if(type=="tool_started"){auto c=tool_call(p);ui.post([&ui,c]{ui.record_tool_start();ui.timeline().start_tool(c);});}else if(type=="tool_completed"){auto r=tool_result(p);ui.post([&ui,r]{ui.record_tool_end();ui.timeline().finish_tool(r);});}else if(type=="approval_requested"){auto c=tool_call(p);auto approval=p.value("approval_id","");ui.post([&ui,&api,c,approval]{ui.request_approval(c,[&api,approval](bool allow){api.resolve_approval(approval,allow);});});}else if(type=="run_completed"||type=="run_failed"||type=="run_cancelled"||type=="run_interrupted"){current_run.clear();ui.post([&ui,type,p]{ui.timeline().commit_active();if(type=="run_failed")ui.timeline().add_system(p.value("error","Run failed"),true);ui.status_bar().set_state(type=="run_cancelled"?"Cancelled":"Idle");ui.finish_remote_run();});}};
    auto start_stream=[&]{stop_stream=false;stream=std::thread([&]{while(!stop_stream){long long cursor;{std::lock_guard lock(state_mutex);cursor=last_seq;}api.stream_events(session_id,cursor,[&](auto f){apply(std::move(f));},stop_stream);if(!stop_stream)std::this_thread::sleep_for(std::chrono::milliseconds(300));}});};
    for(const auto&e:api.events(session_id)["events"])apply({e.value("seq",0LL),e.value("type",""),e},true);start_stream();
    ui.timeline().add_system("Connected to merak serve · session "+session_id);
    ui.set_on_cancel([&]{std::string run;{std::lock_guard lock(state_mutex);run=current_run;}if(!run.empty())api.cancel_run(run);});
    ui.set_on_command([&](std::string input){if(input=="/exit"||input=="/quit"){ui.exit();return;}if(input=="/help"){ui.open_help();return;}if(input=="/context"){ui.open_context();return;}if(input=="/transcript"){ui.open_transcript();return;}if(input=="/tool-calls"){ui.open_tool_browser();return;}if(input=="/tools"){std::ostringstream out;out<<"Tools";for(const auto&t:meta["tools"])out<<"\n"<<t.value("name","")<<"  "<<t.value("source","");for(const auto&m:meta["mcp_servers"])out<<"\nMCP "<<m.value("name","")<<"  "<<(m.value("alive",false)?"connected":"offline");ui.timeline().add_system(out.str());return;}if(input=="/memory"){ui.open_memory();return;}if(input=="/session list"){auto sessions=api.list_sessions()["sessions"];std::ostringstream out;out<<"Sessions";for(const auto&s:sessions)out<<"\n"<<s.value("id","")<<"  "<<s.value("title","");ui.timeline().add_system(out.str());return;}if(input=="/clear")input="/session new";if(input=="/session new"||input.rfind("/session use ",0)==0){if(ui.busy()||ui.queued_messages()>0){ui.timeline().add_system("Cannot switch sessions while a run or queued message is active",true);return;}stop_stream=true;if(stream.joinable())stream.join();session_id=input=="/session new"?api.create_session()["session_id"].get<std::string>():input.substr(13);api.session(session_id);{std::lock_guard lock(state_mutex);last_seq=0;current_run.clear();}ui.reset_timeline();for(const auto&e:api.events(session_id)["events"])apply({e.value("seq",0LL),e.value("type",""),e},true);start_stream();ui.timeline().add_system("Using session "+session_id);return;}if(input.starts_with("/")){ui.timeline().add_system("Unknown command: "+input,true);return;}ui.start_background([&api,&ui,&session_id,input=std::move(input)]{try{api.start_run(session_id,input);}catch(const std::exception&e){ui.post([&ui,error=std::string(e.what())]{ui.timeline().add_system(error,true);ui.finish_remote_run();});}});});
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
