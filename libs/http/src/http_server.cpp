#include <merak/http_server.hpp>
#include <merak/config_loader.hpp>
#include <merak/llm_provider.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace merak {
namespace {
nlohmann::json session_json(const SessionRecord&s){return{{"id",s.id},{"title",s.title},{"world_id",s.world_id},{"agent_id",s.agent_id},{"last_seq",s.last_seq},{"created_at",s.created_at},{"updated_at",s.updated_at},{"archived_at",s.archived_at}};}
long long after(const httplib::Request&r){try{return std::stoll(r.get_param_value("after"));}catch(...){return 0;}}
DelegationRequest delegation_request_from_json(const nlohmann::json&body){
    DelegationRequest request;
    request.pattern=body.value("pattern","fan_out");
    request.task=body.value("task","");
    request.aggregation=body.value("aggregation","all_results");
    for(const auto&agent:body.value("agents",nlohmann::json::array()))request.agent_ids.push_back(agent.get<std::string>());
    return request;
}

std::string run_status_json(RunStatus status) {
    switch (status) {
    case RunStatus::Queued: return "queued";
    case RunStatus::Running: return "running";
    case RunStatus::WaitingApproval: return "waiting_approval";
    case RunStatus::Completed: return "completed";
    case RunStatus::Failed: return "failed";
    case RunStatus::Cancelled: return "cancelled";
    case RunStatus::Interrupted: return "interrupted";
    }
    return "unknown";
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool is_text_extension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    ext = lowercase(ext);
    static const std::set<std::string> allowed{"md","markdown","txt","json","yaml","yml"};
    return allowed.contains(ext);
}

std::string mime_for_extension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    ext = lowercase(ext);
    if (ext == "md" || ext == "markdown") return "text/markdown";
    if (ext == "json") return "application/json";
    if (ext == "yaml" || ext == "yml") return "application/yaml";
    return "text/plain";
}

bool workspace_type_matches(const std::string& type, const std::string& ext) {
    if (type.empty() || type == "all") return true;
    if (type == ext) return true;
    if (type == "markdown") return ext == "md" || ext == "markdown";
    if (type == "text") return ext == "txt";
    if (type == "data") return ext == "json" || ext == "yaml" || ext == "yml";
    return false;
}

std::chrono::system_clock::time_point file_time_to_system(std::filesystem::file_time_type file_time) {
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
}

std::string iso_time(std::chrono::system_clock::time_point time) {
    auto tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

long long file_mtime_seconds(const std::filesystem::path& path) {
    return std::chrono::duration_cast<std::chrono::seconds>(
        file_time_to_system(std::filesystem::last_write_time(path)).time_since_epoch()).count();
}

std::string file_updated_at(const std::filesystem::path& path) {
    return iso_time(file_time_to_system(std::filesystem::last_write_time(path)));
}

std::string file_version(const std::filesystem::path& path) {
    return "mtime:" + std::to_string(file_mtime_seconds(path)) +
        ":size:" + std::to_string(std::filesystem::file_size(path));
}

bool has_hidden_or_runtime_part(const std::filesystem::path& path) {
    for (const auto& part : path) {
        auto s = part.string();
        if (s == ".git") return true;
        if (!s.empty() && s[0] == '.') return true;
        auto lower = lowercase(s);
        if (lower == "runtime.db" || lower == "runtime.sqlite" || lower == "runtime.sqlite3" ||
            lower == "sessions.db" || lower == "sessions.sqlite" || lower == "sessions.sqlite3") {
            return true;
        }
    }
    return false;
}

bool is_under(const std::filesystem::path& child, const std::filesystem::path& parent) {
    auto c = std::filesystem::weakly_canonical(child);
    auto p = std::filesystem::weakly_canonical(parent);
    auto cit = c.begin();
    auto pit = p.begin();
    for (; pit != p.end(); ++pit, ++cit) {
        if (cit == c.end() || *cit != *pit) return false;
    }
    return true;
}

std::filesystem::path safe_existing_or_parent_canonical(const std::filesystem::path& raw) {
    if (std::filesystem::exists(raw)) return std::filesystem::weakly_canonical(raw);
    auto parent = raw.parent_path().empty() ? std::filesystem::current_path() : raw.parent_path();
    return std::filesystem::weakly_canonical(parent) / raw.filename();
}

std::filesystem::path home_root(const std::string& merak_home_path) {
    auto root = merak_home_path.empty() ? std::filesystem::current_path() : std::filesystem::path(merak_home_path);
    std::filesystem::create_directories(root);
    return std::filesystem::weakly_canonical(root);
}

std::filesystem::path resolve_workspace_root(const httplib::Request& req,
                                             const std::filesystem::path& home) {
    std::filesystem::path root;
    if (req.has_param("root") && !req.get_param_value("root").empty()) {
        root = std::filesystem::u8path(req.get_param_value("root"));
        if (root.is_relative()) root = home / root;
    } else if (req.has_param("world_id") && !req.get_param_value("world_id").empty()) {
        auto world_root = home / "worlds" / req.get_param_value("world_id");
        auto outputs = world_root / "outputs";
        root = std::filesystem::exists(outputs) ? outputs : world_root;
    } else {
        root = home / "outputs";
    }
    std::filesystem::create_directories(root);
    root = std::filesystem::weakly_canonical(root);
    if (!is_under(root, home)) throw RuntimeError("invalid_path", "Workspace root must be under merak home");
    return root;
}

nlohmann::json workspace_file_json(const std::filesystem::path& file,
                                   const std::filesystem::path& root) {
    auto relative = std::filesystem::relative(file, root).generic_string();
    auto ext = file.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    return {
        {"id", file.generic_string()},
        {"path", file.generic_string()},
        {"relative_path", relative},
        {"name", file.filename().string()},
        {"ext", lowercase(ext)},
        {"mime", mime_for_extension(file)},
        {"size", static_cast<long long>(std::filesystem::file_size(file))},
        {"updated_at", file_updated_at(file)},
        {"generated_by_run_id", nullptr},
        {"dirty", false}
    };
}

bool open_path_in_system(const std::filesystem::path& target, bool reveal) {
#ifdef _WIN32
    auto path = target.wstring();
    if (reveal && std::filesystem::is_regular_file(target)) {
        std::wstring args = L"/select,\"" + path + L"\"";
        auto result = reinterpret_cast<intptr_t>(
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
        return result > 32;
    }
    auto result = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif __APPLE__
    auto shell_path = target.string();
    size_t pos = 0;
    while ((pos = shell_path.find('\'', pos)) != std::string::npos) {
        shell_path.replace(pos, 1, "'\\''");
        pos += 4;
    }
    auto command = std::string("open '") + shell_path + "'";
    return std::system(command.c_str()) == 0;
#else
    auto shell_path = target.string();
    size_t pos = 0;
    while ((pos = shell_path.find('\'', pos)) != std::string::npos) {
        shell_path.replace(pos, 1, "'\\''");
        pos += 4;
    }
    auto command = std::string("xdg-open '") + shell_path + "' >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
#endif
}
}
HttpServer::HttpServer(std::shared_ptr<RuntimeService>runtime,RuntimeMetadata metadata,
                       std::string merak_home,std::shared_ptr<LlmProvider>llm_provider)
    :runtime_(std::move(runtime)),metadata_(std::move(metadata)),
     merak_home_path_(std::move(merak_home)),llm_provider_(std::move(llm_provider)){
    auto cfg_result=ConfigLoader::load();
    if(cfg_result.has_value()){
        auto&cfg=cfg_result.value();
        cached_config_["provider"]=cfg.llm.provider;
        cached_config_["api_base_url"]=cfg.llm.api_base_url;
        cached_config_["default_model"]=cfg.llm.default_model;
        cached_config_["max_output_tokens"]=cfg.llm.max_output_tokens;
        const auto&key=cfg.llm.api_key;
        if(key.length()>4){
            cached_config_["api_key_masked"]=std::string(key.length()-4,'*')+key.substr(key.length()-4);
        }else{
            cached_config_["api_key_masked"]=key.empty()?"":"****";
        }
    }
    install_routes();}
HttpResult HttpServer::error(const std::string&code,const std::string&message,int status,bool retryable){return{status,{{"error",{{"code",code},{"message",message},{"retryable",retryable}}}}};}
void HttpServer::json(httplib::Response&r,const HttpResult&v){r.status=v.status;r.set_content(v.body.dump(),"application/json");}
HttpResult HttpServer::handle_runtime_metadata()const{nlohmann::json tools=nlohmann::json::array(),mcp=nlohmann::json::array(),agents=nlohmann::json::array(),models=nlohmann::json::array();for(const auto&t:metadata_.tools)tools.push_back({{"name",t.name},{"description",t.description},{"source",t.source},{"requires_confirmation",t.requires_confirmation}});for(const auto&s:metadata_.mcp_servers)mcp.push_back({{"name",s.name},{"alive",s.alive}});for(const auto&a:metadata_.agents)agents.push_back({{"id",a.id},{"description",a.description}});for(const auto&m:metadata_.models)models.push_back({{"name",m.name},{"provider",m.provider},{"max_context_tokens",m.max_context_tokens}});if(models.empty())models.push_back({{"name",metadata_.model},{"provider",metadata_.provider},{"max_context_tokens",128000}});return{200,{{"provider",metadata_.provider},{"model",metadata_.model},{"models",models},{"permission_mode",metadata_.permission_mode},{"memory",{{"enabled",metadata_.memory_enabled}}},{"worldbuilding",{{"enabled",metadata_.worldbuilding_enabled}}},{"tui",{{"theme",metadata_.tui_theme}}},{"tools",tools},{"mcp_servers",mcp},{"agents",agents},{"delegation_patterns",{"fan_out","sequential","pipeline"}}}};}
HttpResult HttpServer::handle_session_memory(const std::string&id)const{try{nlohmann::json items=nlohmann::json::array();int index=0;for(const auto&e:runtime_->events_after(id,0)){if(e.type!="message_appended")continue;items.push_back({{"index",++index},{"role",e.payload.value("role","")},{"content",e.payload.value("content","")},{"tool_call_id",e.payload.value("tool_call_id","")}});}return{200,{{"session_id",id},{"items",items}}};}catch(const RuntimeError&e){return error(e.code(),e.what(),404,e.retryable());}}
HttpResult HttpServer::handle_create_session(const std::string&title,const std::string&world_id,const std::string&agent_id){auto s=runtime_->create_session(title,world_id,agent_id);return{201,{{"session_id",s.id},{"session",session_json(s)}}};}
HttpResult HttpServer::handle_get_session(const std::string&id)const{auto s=runtime_->get_session(id);return s?HttpResult{200,session_json(*s)}:error("session_not_found","Session does not exist",404);}
HttpResult HttpServer::handle_update_session(const std::string&id,const std::string&title){runtime_->update_session(id,title);auto s=runtime_->get_session(id);if(!s)return{404,{{"error","session not found"}}};return{200,{{"session",session_json(*s)}}};}
HttpResult HttpServer::handle_archive_session(const std::string&id,bool archived){try{auto s=runtime_->archive_session(id,archived);return{200,{{"ok",true},{"session",session_json(s)}}};}catch(const RuntimeError&e){return error(e.code(),e.what(),404,e.retryable());}catch(const std::exception&e){return error("session_not_found",e.what(),404);}}
HttpResult HttpServer::handle_run_detail(const std::string&id)const{
    auto run=runtime_->get_run(id);
    if(!run)return error("run_not_found","Run does not exist",404);
    int input_tokens=0,output_tokens=0;
    std::map<std::string,nlohmann::json> tool_calls;
    for(const auto&e:runtime_->events_after(run->session_id,0)){
        if(e.run_id!=id)continue;
        if(e.type=="usage_updated"||e.type=="sub_run_usage_updated"){
            input_tokens+=e.payload.value("input_tokens",0);
            output_tokens+=e.payload.value("output_tokens",0);
        }else if(e.type=="tool_started"||e.type=="sub_run_tool_started"){
            auto call_id=e.payload.value("id","");
            if(call_id.empty())continue;
            tool_calls[call_id]={
                {"id",call_id},
                {"name",e.payload.value("name","")},
                {"arguments",e.payload.value("arguments","")},
                {"status","running"},
                {"started_at",e.timestamp}
            };
        }else if(e.type=="tool_completed"||e.type=="sub_run_tool_completed"){
            auto call_id=e.payload.value("id","");
            if(call_id.empty())continue;
            auto& call=tool_calls[call_id];
            if(!call.contains("id"))call["id"]=call_id;
            if(!call.contains("name"))call["name"]=e.payload.value("name","");
            call["output"]=e.payload.value("output","");
            call["is_error"]=e.payload.value("is_error",false);
            call["status"]=e.payload.value("is_error",false)?"failed":"completed";
            call["completed_at"]=e.timestamp;
        }
    }
    nlohmann::json calls=nlohmann::json::array();
    for(auto&[_,call]:tool_calls)calls.push_back(call);
    return{200,{{"ok",true},{"run",{
        {"id",run->id},
        {"session_id",run->session_id},
        {"status",run_status_json(run->status)},
        {"model",metadata_.model},
        {"started_at",run->started_at},
        {"completed_at",run->finished_at.empty()?nlohmann::json(nullptr):nlohmann::json(run->finished_at)},
        {"input_tokens",input_tokens},
        {"output_tokens",output_tokens},
        {"tool_calls",calls}
    }}}};
}
HttpResult HttpServer::handle_create_delegation(const std::string&id,const DelegationRequest&request){try{auto d=runtime_->start_delegation(id,request);return{202,{{"delegation_id",d.delegation_id},{"parent_run_id",d.parent_run_id},{"session_id",d.session_id}}};}catch(const RuntimeError&e){int status=e.code()=="session_busy"?409:e.code()=="session_not_found"||e.code()=="agent_not_found"?404:400;return error(e.code(),e.what(),status,e.retryable());}}
void HttpServer::install_routes(){
    server_.Get("/v1/runtime",[this](const auto&,auto&r){json(r,handle_runtime_metadata());});
    server_.Get("/api/webui/capabilities",[this](const auto&req,auto&r){handle_capabilities(req,r);});
    server_.Post("/v1/sessions",[this](const auto&req,auto&r){try{auto body=req.body.empty()?nlohmann::json::object():nlohmann::json::parse(req.body);json(r,handle_create_session(body.value("title",""),body.value("world_id",""),body.value("agent_id","")));}catch(const std::exception&e){json(r,error("invalid_request",e.what(),400));}});
    server_.Get("/v1/sessions",[this](const auto&req,auto&r){auto wid=req.has_param("world_id")?req.get_param_value("world_id"):"";nlohmann::json a=nlohmann::json::array();for(const auto&s:runtime_->list_sessions(wid))a.push_back(session_json(s));json(r,{200,{{"sessions",a}}});});
    server_.Get(R"(/v1/sessions/([^/]+))",[this](const auto&req,auto&r){json(r,handle_get_session(req.matches[1]));});
    server_.Patch(R"(/v1/sessions/([^/]+))",[this](const httplib::Request&req,httplib::Response&res){auto id=req.matches[1];auto body=req.body.empty()?nlohmann::json::object():nlohmann::json::parse(req.body);std::string title=body.value("title","");json(res,handle_update_session(id,title));});
    server_.Post(R"(/v1/sessions/([^/]+)/archive)",[this](const httplib::Request&req,httplib::Response&res){try{auto body=req.body.empty()?nlohmann::json::object():nlohmann::json::parse(req.body);json(res,handle_archive_session(req.matches[1],body.value("archived",true)));}catch(const std::exception&e){json(res,error("invalid_request",e.what(),400));}});
    server_.Post(R"(/v1/sessions/([^/]+)/generate-title)",[this](const httplib::Request&req,httplib::Response&res){auto id=req.matches[1];try{std::string title=runtime_->generate_title(id);json(res,{200,{{"title",title}}});}catch(const std::exception&e){json(res,error("title_generation_failed",e.what(),500));}});
    server_.Get(R"(/v1/worlds/([^/]+)/agents/([^/]+)/session)",[this](const auto&req,auto&r){auto world_id=req.matches[1].str();auto agent_id=req.matches[2].str();auto sessions=runtime_->list_sessions(world_id);for(const auto&s:sessions){if(s.agent_id==agent_id&&s.archived_at.empty()){json(r,{200,{{"session",session_json(s)},{"created",false}}});return;}}auto s=runtime_->create_session("",world_id,agent_id);json(r,{201,{{"session",session_json(s)},{"created",true}}});});
    server_.Get(R"(/v1/sessions/([^/]+)/events)",[this](const auto&req,auto&r){try{nlohmann::json a=nlohmann::json::array();for(const auto&e:runtime_->events_after(req.matches[1],after(req)))if(e.type!="message_appended"&&e.type!="compaction_applied")a.push_back(e);json(r,{200,{{"events",a}}});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),404,e.retryable()));}});
    server_.Get(R"(/v1/sessions/([^/]+)/memory)",[this](const auto&req,auto&r){json(r,handle_session_memory(req.matches[1]));});
    server_.Post(R"(/v1/sessions/([^/]+)/runs)",[this](const auto&req,auto&r){try{auto b=nlohmann::json::parse(req.body);auto model=b.value("model",metadata_.model);auto run=runtime_->start_run(req.matches[1],b.value("message",""),model);json(r,{202,{{"run_id",run.id},{"session_id",run.session_id},{"model",model}}});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),e.code()=="session_busy"?409:400,e.retryable()));}catch(const std::exception&e){json(r,error("invalid_request",e.what(),400));}});
    server_.Post(R"(/v1/sessions/([^/]+)/delegations)",[this](const auto&req,auto&r){try{auto b=nlohmann::json::parse(req.body);json(r,handle_create_delegation(req.matches[1],delegation_request_from_json(b)));}catch(const std::exception&e){json(r,error("invalid_request",e.what(),400));}});
    server_.Post(R"(/v1/approvals/([^/]+))",[this](const auto&req,auto&r){try{auto b=nlohmann::json::parse(req.body);auto status=b.value("decision","")=="allow"?ApprovalStatus::Allowed:ApprovalStatus::Denied;auto a=runtime_->resolve_approval(req.matches[1],status);json(r,{200,{{"approval_id",a.id},{"status",to_string(a.status)}}});}catch(const std::exception&e){json(r,error("approval_not_found",e.what(),404));}});
    server_.Post(R"(/v1/creations/([^/]+)/resolve)", [this](const auto& req, auto& r) {
        try {
            auto b = nlohmann::json::parse(req.body);
            std::string decision = b.value("decision", "deny");
            nlohmann::json modifications = b.value("modifications", nlohmann::json::object());
            auto result = runtime_->resolve_creation(req.matches[1], decision, modifications);
            json(r, {200, result});
        } catch (const RuntimeError& e) {
            json(r, error(e.code(), e.what(), 404));
        } catch (const std::exception& e) {
            json(r, error("invalid_request", e.what(), 400));
        }
    });
    server_.Post(R"(/v1/runs/([^/]+)/cancel)",[this](const auto&req,auto&r){try{runtime_->cancel_run(req.matches[1]);json(r,{202,{{"run_id",req.matches[1]},{"status","cancelled"}}});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),404));}});
    server_.Post(R"(/v1/runs/([^/]+)/ask-response)", [this](const auto& req, auto& r) {
        try {
            auto b = nlohmann::json::parse(req.body);
            std::string call_id = b.value("call_id", "");
            std::string response = b.value("response", "");
            if (call_id.empty()) {
                json(r, error("invalid_request", "call_id is required", 400));
                return;
            }
            runtime_->respond_to_ask_user(req.matches[1], call_id, response);
            json(r, {200, {{"ok", true}, {"run_id", req.matches[1]}, {"call_id", call_id}}});
        } catch (const RuntimeError& e) {
            json(r, error(e.code(), e.what(), 404));
        } catch (const std::exception& e) {
            json(r, error("invalid_request", e.what(), 400));
        }
    });
    server_.Get(R"(/v1/runs/([^/]+))",[this](const auto&req,auto&r){json(r,handle_run_detail(req.matches[1]));});
    server_.Get(R"(/v1/sessions/([^/]+)/events/stream)",[this](const auto&req,auto&r){auto id=req.matches[1].str();auto cursor=after(req);try{auto subscription=runtime_->subscribe(id);auto backlog=runtime_->events_after(id,cursor);r.set_chunked_content_provider("text/event-stream",[subscription,backlog=std::move(backlog),cursor](size_t,httplib::DataSink&sink)mutable{auto send=[&](const RuntimeEvent&e){if(e.seq<=cursor||e.type=="message_appended"||e.type=="compaction_applied")return true;auto payload=nlohmann::json(e).dump();auto frame="id: "+std::to_string(e.seq)+"\nevent: "+e.type+"\ndata: "+payload+"\n\n";if(!sink.write(frame.data(),frame.size()))return false;cursor=e.seq;return true;};while(!backlog.empty()){auto e=backlog.front();backlog.erase(backlog.begin());if(!send(e))return false;}RuntimeEvent live;if(subscription->wait_next(live,std::chrono::milliseconds(1000)))return send(live);auto ping=std::string(": keepalive\n\n");return sink.write(ping.data(),ping.size());});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),404));}});
    server_.Get("/api/config/llm", [this](const auto& req, auto& res) { handle_config_get(req, res); });
    server_.Post("/api/config/llm", [this](const auto& req, auto& res) { handle_config_set(req, res); });
    server_.Post("/api/config/llm/test", [this](const auto& req, auto& res) { handle_config_test(req, res); });
    server_.Get("/api/workspace/files", [this](const auto& req, auto& res) { handle_workspace_files_list(req, res); });
    server_.Get("/api/workspace/files/content", [this](const auto& req, auto& res) { handle_workspace_file_content_get(req, res); });
    server_.Put("/api/workspace/files/content", [this](const auto& req, auto& res) { handle_workspace_file_content_put(req, res); });
    server_.Post("/api/workspace/open", [this](const auto& req, auto& res) { handle_workspace_open(req, res); });
}
void HttpServer::handle_capabilities(const httplib::Request&, httplib::Response& res) {
    json(res, {200, {
        {"ok", true},
        {"capabilities", {
            {"files", true},
            {"story_overview", true},
            {"session_archive", true},
            {"world_create", true},
            {"editor_save", true}
        }}
    }});
}

void HttpServer::handle_workspace_files_list(const httplib::Request& req, httplib::Response& res) {
    try {
        auto home = home_root(merak_home_path_);
        auto root = resolve_workspace_root(req, home);
        auto q = lowercase(req.has_param("q") ? req.get_param_value("q") : "");
        auto type = lowercase(req.has_param("type") ? req.get_param_value("type") : "");
        nlohmann::json files = nlohmann::json::array();

        if (std::filesystem::exists(root)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                const auto& path = entry.path();
                if (!entry.is_regular_file()) continue;
                auto relative = std::filesystem::relative(path, root);
                if (has_hidden_or_runtime_part(relative)) continue;
                if (!is_text_extension(path)) continue;
                auto ext = path.extension().string();
                if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
                ext = lowercase(ext);
                if (!workspace_type_matches(type, ext)) continue;
                auto haystack = lowercase(relative.generic_string());
                if (!q.empty() && haystack.find(q) == std::string::npos) continue;
                files.push_back(workspace_file_json(path, root));
            }
        }

        json(res, {200, {{"ok", true}, {"root", root.generic_string()}, {"files", files}}});
    } catch (const RuntimeError& e) {
        json(res, error(e.code(), e.what(), 403, e.retryable()));
    } catch (const std::exception& e) {
        json(res, error("workspace_list_failed", e.what(), 500));
    }
}

void HttpServer::handle_workspace_file_content_get(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!req.has_param("path") || req.get_param_value("path").empty()) {
            json(res, error("invalid_request", "path is required", 400));
            return;
        }
        auto home = home_root(merak_home_path_);
        auto raw = std::filesystem::u8path(req.get_param_value("path"));
        if (raw.is_relative()) raw = home / raw;
        auto path = safe_existing_or_parent_canonical(raw);
        if (!is_under(path, home)) {
            json(res, error("invalid_path", "File must be under merak home", 403));
            return;
        }
        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
            json(res, error("file_not_found", "File does not exist", 404));
            return;
        }
        if (!is_text_extension(path)) {
            json(res, error("unsupported_file_type", "Only text workspace files can be read", 415));
            return;
        }
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        json(res, {200, {{"ok", true}, {"file", {
            {"path", path.generic_string()},
            {"content", buffer.str()},
            {"encoding", "utf-8"},
            {"updated_at", file_updated_at(path)},
            {"version", file_version(path)}
        }}}});
    } catch (const std::exception& e) {
        json(res, error("file_read_failed", e.what(), 500));
    }
}

void HttpServer::handle_workspace_file_content_put(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        auto path_value = body.value("path", "");
        if (path_value.empty()) {
            json(res, error("invalid_request", "path is required", 400));
            return;
        }
        if (!body.contains("content") || !body["content"].is_string()) {
            json(res, error("invalid_request", "content is required", 400));
            return;
        }
        auto home = home_root(merak_home_path_);
        auto raw = std::filesystem::u8path(path_value);
        if (raw.is_relative()) raw = home / raw;
        auto path = safe_existing_or_parent_canonical(raw);
        if (!is_under(path, home)) {
            json(res, error("invalid_path", "File must be under merak home", 403));
            return;
        }
        if (!is_text_extension(path)) {
            json(res, error("unsupported_file_type", "Only text workspace files can be saved", 415));
            return;
        }
        if (std::filesystem::exists(path) && body.contains("version") && !body["version"].is_null()) {
            auto expected = body["version"].get<std::string>();
            if (!expected.empty() && expected != file_version(path)) {
                json(res, error("file_conflict", "File changed since it was opened", 409, true));
                return;
            }
        }
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << body["content"].get<std::string>();
        out.close();

        auto updated_at = file_updated_at(path);
        auto version = file_version(path);
        if (body.contains("session_id") && body["session_id"].is_string() && !body["session_id"].get<std::string>().empty()) {
            try {
                runtime_->emit_event(body["session_id"].get<std::string>(),
                                     body.value("run_id", ""),
                                     "workspace_file_updated",
                                     {{"path", path.generic_string()},
                                      {"version", version},
                                      {"updated_at", updated_at},
                                      {"run_id", body.value("run_id", "")}});
            } catch (const std::exception& e) {
                spdlog::debug("workspace save SSE skipped: {}", e.what());
            }
        }

        json(res, {200, {{"ok", true}, {"file", {
            {"path", path.generic_string()},
            {"updated_at", updated_at},
            {"version", version}
        }}}});
    } catch (const nlohmann::json::exception& e) {
        json(res, error("invalid_request", e.what(), 400));
    } catch (const std::exception& e) {
        json(res, error("file_save_failed", e.what(), 500));
    }
}

void HttpServer::listen(int port){if(!server_.listen("127.0.0.1",port))throw std::runtime_error("Failed to bind to port "+std::to_string(port)+" — already in use?");}
void HttpServer::stop(){server_.stop();}
void HttpServer::serve_static_dir(const std::string& mount_point, const std::string& dir_path) {
    server_.set_mount_point(mount_point.c_str(), dir_path.c_str());
}
void HttpServer::handle_config_get(const httplib::Request&, httplib::Response& res) {
    if (cached_config_.is_null()) {
        json(res, error("config_load_failed", "no config loaded", 500));
        return;
    }
    res.set_content(cached_config_.dump(), "application/json");
}
void HttpServer::handle_config_set(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        auto local_path = std::filesystem::path(merak_home_path_) / "settings.local.json";

        nlohmann::json existing;
        if (std::filesystem::exists(local_path)) {
            std::ifstream in(local_path);
            if (in) existing = nlohmann::json::parse(in, nullptr, false);
            if (existing.is_discarded()) existing = nlohmann::json::object();
        }

        if (!existing.contains("llm")) existing["llm"] = nlohmann::json::object();
        if (body.contains("provider")) existing["llm"]["provider"] = body["provider"];
        if (body.contains("api_key") && body["api_key"].get<std::string>() != "") {
            existing["llm"]["api_key"] = body["api_key"];
        }
        if (body.contains("api_base_url")) existing["llm"]["api_base_url"] = body["api_base_url"];
        if (body.contains("default_model")) existing["llm"]["default_model"] = body["default_model"];
        if (body.contains("max_output_tokens")) existing["llm"]["max_output_tokens"] = body["max_output_tokens"];

        std::filesystem::create_directories(local_path.parent_path());
        std::ofstream out(local_path);
        out << existing.dump(2);

        nlohmann::json resp;
        resp["ok"]=true;
        resp["restart_required"]=true;
        res.set_content(resp.dump(), "application/json");
    } catch (const std::exception& e) {
        json(res, error("config_save_failed", e.what(), 400));
    }
}
void HttpServer::handle_config_test(const httplib::Request& req, httplib::Response& res) {
    if (!llm_provider_) {
        json(res, error("test_unavailable", "LLM provider not available", 503));
        return;
    }
    try {
        auto test_result = llm_provider_->test_connection();
        if (test_result) {
            res.set_content("{\"ok\":true,\"test\":\"passed\"}", "application/json");
        } else {
            json(res, error("test_failed", "LLM connection test failed", 502));
        }
    } catch (const std::exception& e) {
        json(res, error("test_failed", e.what(), 502));
    }
}
void HttpServer::handle_workspace_open(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        auto raw_path = body.value("path", "");
        auto reveal = body.value("reveal", false);
        if (raw_path.empty()) {
            json(res, error("invalid_path", "path is required", 400));
            return;
        }

        std::filesystem::path target = std::filesystem::u8path(raw_path);
        if (!std::filesystem::exists(target)) {
            json(res, error("path_not_found", "Path does not exist", 404));
            return;
        }

        target = std::filesystem::weakly_canonical(target);
        if (!open_path_in_system(target, reveal)) {
            json(res, error("open_failed", "Could not open path", 500));
            return;
        }

        res.set_content(nlohmann::json{{"ok", true}, {"path", target.string()}}.dump(), "application/json");
    } catch (const std::exception& e) {
        json(res, error("open_failed", e.what(), 500));
    }
}
} // namespace merak
