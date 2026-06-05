#include <merak/http_server.hpp>
#include <fstream>

namespace merak {
namespace {
nlohmann::json session_json(const SessionRecord&s){return{{"id",s.id},{"title",s.title},{"last_seq",s.last_seq},{"created_at",s.created_at},{"updated_at",s.updated_at},{"archived_at",s.archived_at}};}
long long after(const httplib::Request&r){try{return std::stoll(r.get_param_value("after"));}catch(...){return 0;}}
DelegationRequest delegation_request_from_json(const nlohmann::json&body){
    DelegationRequest request;
    request.pattern=body.value("pattern","fan_out");
    request.task=body.value("task","");
    request.aggregation=body.value("aggregation","all_results");
    for(const auto&agent:body.value("agents",nlohmann::json::array()))request.agent_ids.push_back(agent.get<std::string>());
    return request;
}
}
HttpServer::HttpServer(std::shared_ptr<RuntimeService>runtime,RuntimeMetadata metadata,
                       std::string merak_home):runtime_(std::move(runtime)),metadata_(std::move(metadata)),
      merak_home_path_(std::move(merak_home)){install_routes();}
HttpResult HttpServer::error(const std::string&code,const std::string&message,int status,bool retryable){return{status,{{"error",{{"code",code},{"message",message},{"retryable",retryable}}}}};}
void HttpServer::json(httplib::Response&r,const HttpResult&v){r.status=v.status;r.set_content(v.body.dump(),"application/json");}
HttpResult HttpServer::handle_runtime_metadata()const{nlohmann::json tools=nlohmann::json::array(),mcp=nlohmann::json::array(),agents=nlohmann::json::array(),models=nlohmann::json::array();for(const auto&t:metadata_.tools)tools.push_back({{"name",t.name},{"description",t.description},{"source",t.source}});for(const auto&s:metadata_.mcp_servers)mcp.push_back({{"name",s.name},{"alive",s.alive}});for(const auto&a:metadata_.agents)agents.push_back({{"id",a.id},{"description",a.description}});for(const auto&m:metadata_.models)models.push_back({{"name",m.name},{"provider",m.provider},{"max_context_tokens",m.max_context_tokens}});if(models.empty())models.push_back({{"name",metadata_.model},{"provider",metadata_.provider},{"max_context_tokens",128000}});return{200,{{"provider",metadata_.provider},{"model",metadata_.model},{"models",models},{"permission_mode",metadata_.permission_mode},{"memory",{{"enabled",metadata_.memory_enabled}}},{"tools",tools},{"mcp_servers",mcp},{"agents",agents},{"delegation_patterns",{"fan_out","sequential","pipeline"}}}};}
HttpResult HttpServer::handle_session_memory(const std::string&id)const{try{nlohmann::json items=nlohmann::json::array();int index=0;for(const auto&e:runtime_->events_after(id,0)){if(e.type!="message_appended")continue;items.push_back({{"index",++index},{"role",e.payload.value("role","")},{"content",e.payload.value("content","")},{"tool_call_id",e.payload.value("tool_call_id","")}});}return{200,{{"session_id",id},{"items",items}}};}catch(const RuntimeError&e){return error(e.code(),e.what(),404,e.retryable());}}
HttpResult HttpServer::handle_create_session(const std::string&title){auto s=runtime_->create_session(title);return{201,{{"session_id",s.id},{"session",session_json(s)}}};}
HttpResult HttpServer::handle_get_session(const std::string&id)const{auto s=runtime_->get_session(id);return s?HttpResult{200,session_json(*s)}:error("session_not_found","Session does not exist",404);}
HttpResult HttpServer::handle_create_delegation(const std::string&id,const DelegationRequest&request){try{auto d=runtime_->start_delegation(id,request);return{202,{{"delegation_id",d.delegation_id},{"parent_run_id",d.parent_run_id},{"session_id",d.session_id}}};}catch(const RuntimeError&e){int status=e.code()=="session_busy"?409:e.code()=="session_not_found"||e.code()=="agent_not_found"?404:400;return error(e.code(),e.what(),status,e.retryable());}}
void HttpServer::install_routes(){
    server_.Get("/v1/runtime",[this](const auto&,auto&r){json(r,handle_runtime_metadata());});
    server_.Post("/v1/sessions",[this](const auto&req,auto&r){try{auto body=req.body.empty()?nlohmann::json::object():nlohmann::json::parse(req.body);json(r,handle_create_session(body.value("title","")));}catch(const std::exception&e){json(r,error("invalid_request",e.what(),400));}});
    server_.Get("/v1/sessions",[this](const auto&,auto&r){nlohmann::json a=nlohmann::json::array();for(const auto&s:runtime_->list_sessions())a.push_back(session_json(s));json(r,{200,{{"sessions",a}}});});
    server_.Get(R"(/v1/sessions/([^/]+))",[this](const auto&req,auto&r){json(r,handle_get_session(req.matches[1]));});
    server_.Get(R"(/v1/sessions/([^/]+)/events)",[this](const auto&req,auto&r){try{nlohmann::json a=nlohmann::json::array();for(const auto&e:runtime_->events_after(req.matches[1],after(req)))if(e.type!="message_appended"&&e.type!="compaction_applied")a.push_back(e);json(r,{200,{{"events",a}}});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),404,e.retryable()));}});
    server_.Get(R"(/v1/sessions/([^/]+)/memory)",[this](const auto&req,auto&r){json(r,handle_session_memory(req.matches[1]));});
    server_.Post(R"(/v1/sessions/([^/]+)/runs)",[this](const auto&req,auto&r){try{auto b=nlohmann::json::parse(req.body);auto model=b.value("model",metadata_.model);auto run=runtime_->start_run(req.matches[1],b.value("message",""),model);json(r,{202,{{"run_id",run.id},{"session_id",run.session_id},{"model",model}}});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),e.code()=="session_busy"?409:400,e.retryable()));}catch(const std::exception&e){json(r,error("invalid_request",e.what(),400));}});
    server_.Post(R"(/v1/sessions/([^/]+)/delegations)",[this](const auto&req,auto&r){try{auto b=nlohmann::json::parse(req.body);json(r,handle_create_delegation(req.matches[1],delegation_request_from_json(b)));}catch(const std::exception&e){json(r,error("invalid_request",e.what(),400));}});
    server_.Post(R"(/v1/approvals/([^/]+))",[this](const auto&req,auto&r){try{auto b=nlohmann::json::parse(req.body);auto status=b.value("decision","")=="allow"?ApprovalStatus::Allowed:ApprovalStatus::Denied;auto a=runtime_->resolve_approval(req.matches[1],status);json(r,{200,{{"approval_id",a.id},{"status",to_string(a.status)}}});}catch(const std::exception&e){json(r,error("approval_not_found",e.what(),404));}});
    server_.Post(R"(/v1/runs/([^/]+)/cancel)",[this](const auto&req,auto&r){try{runtime_->cancel_run(req.matches[1]);json(r,{202,{{"run_id",req.matches[1]},{"status","cancelled"}}});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),404));}});
    server_.Get(R"(/v1/sessions/([^/]+)/events/stream)",[this](const auto&req,auto&r){auto id=req.matches[1].str();auto cursor=after(req);try{auto subscription=runtime_->subscribe(id);auto backlog=runtime_->events_after(id,cursor);r.set_chunked_content_provider("text/event-stream",[subscription,backlog=std::move(backlog),cursor](size_t,httplib::DataSink&sink)mutable{auto send=[&](const RuntimeEvent&e){if(e.seq<=cursor||e.type=="message_appended"||e.type=="compaction_applied")return true;auto payload=nlohmann::json(e).dump();auto frame="id: "+std::to_string(e.seq)+"\nevent: "+e.type+"\ndata: "+payload+"\n\n";if(!sink.write(frame.data(),frame.size()))return false;cursor=e.seq;return true;};while(!backlog.empty()){auto e=backlog.front();backlog.erase(backlog.begin());if(!send(e))return false;}RuntimeEvent live;if(subscription->wait_next(live,std::chrono::milliseconds(1000)))return send(live);auto ping=std::string(": keepalive\n\n");return sink.write(ping.data(),ping.size());});}catch(const RuntimeError&e){json(r,error(e.code(),e.what(),404));}});
    server_.Get("/api/config/llm", [this](const auto& req, auto& res) { handle_config_get(req, res); });
    server_.Post("/api/config/llm", [this](const auto& req, auto& res) { handle_config_set(req, res); });
}
void HttpServer::listen(int port){server_.listen("127.0.0.1",port);}
void HttpServer::stop(){server_.stop();}
void HttpServer::serve_static_dir(const std::string& mount_point, const std::string& dir_path) {
    server_.set_mount_point(mount_point.c_str(), dir_path.c_str());
}
void HttpServer::handle_config_get(const httplib::Request&, httplib::Response& res) {
    auto cfg_result = ConfigLoader::load();
    if (!cfg_result.has_value()) {
        json(res, error("config_load_failed", cfg_result.error().what(), 500));
        return;
    }
    auto& cfg = cfg_result.value();
    nlohmann::json body;
    body["provider"] = cfg.llm.provider;
    body["api_base_url"] = cfg.llm.api_base_url;
    body["default_model"] = cfg.llm.default_model;
    body["max_output_tokens"] = cfg.llm.max_output_tokens;
    const auto& key = cfg.llm.api_key;
    if (key.length() > 4) {
        body["api_key_masked"] = std::string(key.length() - 4, '*') + key.substr(key.length() - 4);
    } else {
        body["api_key_masked"] = key.empty() ? "" : "****";
    }
    res.set_content(body.dump(), "application/json");
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

        res.set_content("{\"ok\":true}", "application/json");
    } catch (const std::exception& e) {
        json(res, error("config_save_failed", e.what(), 400));
    }
}
} // namespace merak
