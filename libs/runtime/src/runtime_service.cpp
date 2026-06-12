#include <merak/runtime_service.hpp>
#include <merak/turn_state.hpp>
#include <merak/prompts/compositor.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/pipeline_manager.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <thread>
#include <merak/interruption.hpp>

namespace merak {
namespace {
std::string make_runtime_id(const char* prefix) {
    static std::atomic<unsigned long long> n = 0;
    auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string(prefix) + "_" + std::to_string(ticks) + "_" + std::to_string(++n);
}
nlohmann::json message_json(const Message& m) {
    nlohmann::json j{{"role",m.role},{"content",m.content},
        {"provider_content_blocks_json",m.provider_content_blocks_json}};
    if(m.tool_call_id)j["tool_call_id"]=*m.tool_call_id;
    j["tool_calls"]=nlohmann::json::array();
    for(const auto& c:m.tool_calls)j["tool_calls"].push_back({{"id",c.id},{"name",c.name},{"arguments",c.arguments}});
    return j;
}
Message message_from_json(const nlohmann::json& j) {
    Message m; m.role=j.value("role","");m.content=j.value("content","");
    m.provider_content_blocks_json=j.value("provider_content_blocks_json","");
    if(j.contains("tool_call_id"))m.tool_call_id=j["tool_call_id"].get<std::string>();
    for(const auto& c:j.value("tool_calls",nlohmann::json::array()))
        m.tool_calls.push_back({c.value("id",""),c.value("name",""),c.value("arguments","")});
    return m;
}
std::string state_name_json(TurnState s){return state_name(s);}
bool valid_pattern(const std::string& pattern) {
    return pattern == "fan_out" || pattern == "sequential" || pattern == "pipeline";
}
bool valid_aggregation(const std::string& aggregation) {
    return aggregation == "all_results" || aggregation == "first_success";
}
std::string run_step_name(TurnState state) {
    auto name = state_name_json(state);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (name == "thinking" || name == "acting" || name == "observing" || name == "responding")
        return name;
    return "";
}
std::string join_agent_outputs(const std::vector<AgentResponse>& responses,
                               const std::vector<std::string>& agent_ids,
                               const std::string& aggregation) {
    std::ostringstream out;
    for (size_t i = 0; i < responses.size(); ++i) {
        if (aggregation == "first_success" && responses[i].text.empty()) continue;
        if (out.tellp() > 0) out << "\n---\n";
        out << "[" << agent_ids[i] << "] " << responses[i].text;
        if (aggregation == "first_success") break;
    }
    return out.str();
}
}
bool EventSubscription::wait_next(RuntimeEvent&e,std::chrono::milliseconds timeout){std::unique_lock lock(mutex_);changed_.wait_for(lock,timeout,[&]{return closed_||!events_.empty();});if(events_.empty())return false;e=std::move(events_.front());events_.pop_front();return true;}
void EventSubscription::push(const RuntimeEvent&e){std::lock_guard lock(mutex_);if(closed_)return;if(events_.size()>=256){closed_=true;changed_.notify_all();return;}events_.push_back(e);changed_.notify_one();}
void EventSubscription::close(){std::lock_guard lock(mutex_);closed_=true;changed_.notify_all();}
std::shared_ptr<EventSubscription>EventBus::subscribe(const std::string&id){auto s=std::make_shared<EventSubscription>();std::lock_guard lock(mutex_);subscriptions_[id].push_back(s);return s;}
void EventBus::publish(const RuntimeEvent&e){std::lock_guard lock(mutex_);auto&v=subscriptions_[e.session_id];for(auto it=v.begin();it!=v.end();){if(auto s=it->lock()){s->push(e);++it;}else it=v.erase(it);}}

class RuntimeService::Control final : public RunControl {
public:
    Control(RuntimeService& service, RunRecord run, std::shared_ptr<CancellationToken> token)
        : service_(service), run_(std::move(run)), token_(std::move(token)) {
        save_checkpoint = [this](int turn_index, const std::string& turn_state_json,
                                  int64_t input_tokens, int64_t output_tokens,
                                  const std::string& pending_calls_json,
                                  const std::string& compacted_summary,
                                  const std::string& pipeline_snapshot_json) {
            service_.store_->save_checkpoint(
                make_runtime_id("ckpt"), run_.id, turn_index, turn_state_json,
                input_tokens, output_tokens, pending_calls_json,
                compacted_summary, pipeline_snapshot_json);
        };
    }
    nlohmann::json base_payload(nlohmann::json payload = {}) const {
        if (!run_.parent_run_id.empty()) payload["parent_run_id"] = run_.parent_run_id;
        if (!run_.delegation_id.empty()) payload["delegation_id"] = run_.delegation_id;
        if (!run_.agent_id.empty()) payload["agent_id"] = run_.agent_id;
        payload["run_kind"] = run_.run_kind;
        return payload;
    }
    std::string event_name(const std::string& normal) const {
        if (run_.run_kind != "sub_run") return normal;
        if (normal == "state_changed") return "sub_run_state_changed";
        if (normal == "text_delta") return "sub_run_text_delta";
        if (normal == "tool_started") return "sub_run_tool_started";
        if (normal == "tool_completed") return "sub_run_tool_completed";
        if (normal == "usage_updated") return "sub_run_usage_updated";
        return normal;
    }
    void emit_state(TurnState from,TurnState to)override{
        service_.emit(run_.session_id,run_.id,event_name("state_changed"),base_payload({{"from",state_name_json(from)},{"to",state_name_json(to)}}));
        auto step = run_step_name(to);
        if (!step.empty()) {
            service_.emit(run_.session_id,run_.id,"run_step_changed",base_payload({
                {"run_id",run_.id},
                {"step",step},
                {"label",state_name_json(to)}
            }));
        }
    }
    void emit_text_delta(std::string text)override{service_.emit(run_.session_id,run_.id,event_name("text_delta"),base_payload({{"text",std::move(text)}}));}
    void emit_tool_started(const ToolCall&c)override{service_.emit(run_.session_id,run_.id,event_name("tool_started"),base_payload({{"id",c.id},{"name",c.name},{"arguments",c.arguments}}));}
    void emit_tool_completed(const ToolCall&c,const ToolResult&r)override{
        service_.emit(run_.session_id,run_.id,event_name("tool_completed"),base_payload({{"id",c.id},{"name",c.name},{"output",r.output},{"is_error",r.is_error}}));
        if (r.is_error || c.name != "write_file") return;
        try {
            auto output = nlohmann::json::parse(r.output);
            auto path_value = output.value("path", "");
            if (path_value.empty()) return;
            std::filesystem::path path(path_value);
            auto event_type = output.value("created", false)
                ? "workspace_file_created"
                : "workspace_file_updated";
            nlohmann::json payload{
                {"path", path_value},
                {"name", path.filename().string()},
                {"ext", path.has_extension() ? path.extension().string().substr(1) : ""},
                {"run_id", run_.id},
                {"session_id", run_.session_id}
            };
            if (output.contains("bytes_written")) payload["size"] = output["bytes_written"];
            service_.emit(run_.session_id,run_.id,event_type,base_payload(std::move(payload)));
        } catch (const std::exception& e) {
            spdlog::debug("write_file result did not produce workspace file SSE: {}", e.what());
        }
    }
    bool await_approval(const ToolCall&c)override{
        ApprovalRecord a; a.run_id=run_.id;a.tool_name=c.name;a.arguments_json=c.arguments;a.tool_call_id=c.id;
        a=service_.store_->create_approval(std::move(a));service_.store_->update_run_status(run_.id,RunStatus::WaitingApproval);
        service_.emit(run_.session_id,run_.id,"approval_requested",base_payload({{"approval_id",a.id},{"tool",c.name},{"arguments",c.arguments},{"tool_call_id",c.id}}));
        service_.emit(run_.session_id,run_.id,"run_step_changed",base_payload({{"run_id",run_.id},{"step","waiting_approval"},{"label","Waiting approval"},{"detail",c.name}}));
        std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return decision_.has_value()||token_->cancelled();});
        if(token_->cancelled())return false;service_.store_->update_run_status(run_.id,RunStatus::Running);return *decision_;
    }
    ToolResult await_creation(const ToolCall& call, const ToolResult& preliminary_result) override {
        auto prelim_json = nlohmann::json::parse(preliminary_result.output);
        std::string creation_id = prelim_json.value("creation_id", "");
        awaiting_creation_id_ = creation_id;

        if (!service_.wb_service_) {
            ToolResult error_result;
            error_result.call_id = call.id;
            error_result.is_error = true;
            error_result.output = R"({"ok":false,"error":{"code":"NOT_CONFIGURED","message":"worldbuilding service not configured"}})";
            return error_result;
        }

        auto pending = service_.wb_service_->get_pending_creation(creation_id);
        if (!pending) {
            ToolResult error_result;
            error_result.call_id = call.id;
            error_result.is_error = true;
            error_result.output = R"({"ok":false,"error":{"code":"NOT_FOUND","message":"creation not found"}})";
            return error_result;
        }

        service_.store_->update_run_status(run_.id, RunStatus::WaitingApproval);
        service_.emit(run_.session_id, run_.id, "creation_requested", base_payload({
            {"creation_id", creation_id},
            {"tool", pending->tool_name},
            {"preview", pending->preview}
        }));

        bool timed_out = false;
        {
            std::unique_lock lock(creation_mutex_);
            bool woke = creation_changed_.wait_for(lock, std::chrono::minutes(5), [&] {
                return creation_resolved_.has_value() || token_->cancelled();
            });
            if (!woke && !token_->cancelled() && !creation_resolved_.has_value()) {
                timed_out = true;
            }
        }

        if (timed_out) {
            // Auto-resolve as deny on timeout
            if (service_.wb_service_) {
                service_.wb_service_->resolve_creation(awaiting_creation_id_, "deny", {});
            }

            // Emit timeout SSE event
            service_.emit(run_.session_id, run_.id, "creation_resolved", {
                {"creation_id", awaiting_creation_id_},
                {"tool", "unknown"},
                {"decision", "deny"},
                {"result", {{"ok", true}, {"decision", "deny"}, {"reason", "timeout"}}}
            });

            service_.store_->update_run_status(run_.id, RunStatus::Running);

            ToolResult timeout_result;
            timeout_result.call_id = call.id;
            timeout_result.is_error = true;
            timeout_result.output = R"({"ok":false,"error":{"code":"TIMEOUT","message":"Creation confirmation timed out"}})";
            return timeout_result;
        }

        service_.store_->update_run_status(run_.id, RunStatus::Running);

        if (token_->cancelled()) {
            ToolResult cancelled;
            cancelled.call_id = call.id;
            cancelled.is_error = true;
            cancelled.output = R"({"ok":false,"error":{"code":"CANCELLED","message":"run cancelled"}})";
            return cancelled;
        }

        ToolResult final_result;
        final_result.call_id = call.id;
        final_result.output = creation_resolved_->dump();
        creation_resolved_.reset();
        return final_result;
    }
    ToolResult await_ask_user(const ToolCall& call, const ToolResult& pending_result) override {
        auto pending_json = nlohmann::json::parse(pending_result.output);
        std::string question = pending_json.value("question", "");
        auto options = pending_json.value("options", nlohmann::json::array());
        bool multi_select = pending_json.value("multi_select", false);

        awaiting_ask_user_call_id_ = call.id;

        service_.store_->update_run_status(run_.id, RunStatus::WaitingApproval);
        service_.emit(run_.session_id, run_.id, "ask_user_requested", base_payload({
            {"call_id", call.id},
            {"question", question},
            {"options", options},
            {"multi_select", multi_select}
        }));

        bool timed_out = false;
        {
            std::unique_lock lock(ask_user_mutex_);
            bool woke = ask_user_changed_.wait_for(lock, std::chrono::minutes(5), [&] {
                return ask_user_response_.has_value() || token_->cancelled();
            });
            if (!woke && !token_->cancelled() && !ask_user_response_.has_value()) {
                timed_out = true;
            }
        }

        service_.store_->update_run_status(run_.id, RunStatus::Running);

        if (timed_out || token_->cancelled()) {
            ToolResult timeout_result;
            timeout_result.call_id = call.id;
            timeout_result.output = nlohmann::json{
                {"status", "error"},
                {"message", timed_out ? "AskUser timed out" : "Run cancelled"}
            }.dump();
            return timeout_result;
        }

        ToolResult final_result;
        final_result.call_id = call.id;
        final_result.output = nlohmann::json{
            {"status", "ok"},
            {"question", question},
            {"answer", *ask_user_response_}
        }.dump();
        ask_user_response_.reset();
        return final_result;
    }
    void resolve_ask_user(const std::string& call_id, const std::string& response) {
        std::lock_guard lock(ask_user_mutex_);
        if (awaiting_ask_user_call_id_ == call_id) {
            ask_user_response_ = response;
            ask_user_changed_.notify_all();
        }
    }
    const std::string& awaiting_ask_user_call_id() const { return awaiting_ask_user_call_id_; }
    void resolve_creation_result(nlohmann::json result) {
        std::lock_guard lock(creation_mutex_);
        creation_resolved_ = std::move(result);
        creation_changed_.notify_all();
    }
    const std::string& awaiting_creation_id() const { return awaiting_creation_id_; }
    void resolve(bool allowed){std::lock_guard lock(mutex_);decision_=allowed;changed_.notify_all();}
    void record_interruption(InterruptionRecord rec) override {
        service_.emit(run_.session_id, run_.id, "run_interrupted", base_payload({
            {"run_id", run_.id},
            {"turns_completed", rec.turns_completed},
            {"tools_completed", rec.tools_completed},
            {"tools_remaining", rec.tools_remaining},
            {"interrupted_tool_name", rec.interrupted_tool_name},
            {"interrupted_tool_call_id", rec.interrupted_tool_call_id},
        }));
    }
    void cancel(){token_->cancel();std::lock_guard lock(mutex_);changed_.notify_all();{std::lock_guard lock2(creation_mutex_);creation_changed_.notify_all();}{std::lock_guard lock3(ask_user_mutex_);ask_user_changed_.notify_all();}}
    void emit_usage(int in,int out,bool exact)override{service_.emit(run_.session_id,run_.id,event_name("usage_updated"),base_payload({{"input_tokens",in},{"output_tokens",out},{"exact",exact}}));}
    void append_message(const Message&m)override{service_.emit(run_.session_id,run_.id,"message_appended",message_json(m));}
    void record_compaction(int count)override{service_.emit(run_.session_id,run_.id,"compaction_applied",{{"replaced_count",count}});}
    bool cancelled()const override{return token_->cancelled();}
    std::shared_ptr<CancellationToken>cancellation_token()const override{return token_;}
private:
    RuntimeService&service_;RunRecord run_;std::shared_ptr<CancellationToken>token_;std::mutex mutex_;std::condition_variable changed_;std::optional<bool>decision_;
    std::mutex creation_mutex_;std::condition_variable creation_changed_;std::optional<nlohmann::json>creation_resolved_;std::string awaiting_creation_id_;
    std::mutex ask_user_mutex_;std::condition_variable ask_user_changed_;std::optional<std::string>ask_user_response_;std::string awaiting_ask_user_call_id_;
};

std::string RuntimeService::extract_title(const std::string& message, size_t max_len) {
    size_t start = 0;
    while (start < message.size() && (message[start] == ' ' || message[start] == '\n' || message[start] == '\r' || message[start] == '\t'))
        start++;
    if (start >= message.size()) return "";

    size_t end = message.size();
    while (end > start && (message[end-1] == ' ' || message[end-1] == '\n' || message[end-1] == '\r' || message[end-1] == '\t'))
        end--;

    std::string trimmed = message.substr(start, end - start);

    size_t nl = trimmed.find('\n');
    if (nl != std::string::npos) trimmed = trimmed.substr(0, nl);

    if (trimmed.size() > max_len) trimmed = trimmed.substr(0, max_len);

    return trimmed;
}

RuntimeService::RuntimeService(std::shared_ptr<SessionStore> store,LoopFactory factory,std::map<std::string, SubAgentConfig> agents,SubRunExecutor sub_run_executor):store_(std::move(store)),loop_factory_(std::move(factory)),agents_(std::move(agents)),sub_run_executor_(std::move(sub_run_executor)){}
void RuntimeService::initialize(){store_->initialize();for(const auto&r:store_->interrupt_running_runs())emit(r.session_id,r.id,"run_interrupted",{{"reason","server restarted"}});}
RuntimeEvent RuntimeService::emit(const std::string&s,const std::string&r,const std::string&t,nlohmann::json p){RuntimeEvent e{0,"",s,r,t,std::move(p)};try{e=store_->append_event(e);}catch(const nlohmann::json::exception&ex){spdlog::warn("Failed to serialize event {}: {}",t,ex.what());}catch(const std::exception&){throw;}bus_.publish(e);return e;}
SessionRecord RuntimeService::create_session(const std::string& title, const std::string& world_id, const std::string& agent_id) {
    auto s = store_->create_session(title, world_id, agent_id);
    emit(s.id, "", "session_created", {{"title", title}});
    if (!world_id.empty()) register_session_world(s.id, world_id);
    if (!world_id.empty() && pipeline_mgr_) {
        if (!pipeline_mgr_->get_state(world_id)) {
            pipeline_mgr_->init_state_for_world(world_id);
        }
    }
    return *store_->get_session(s.id);
}
void RuntimeService::update_session(const std::string&id,const std::string&title){store_->update_session(id,title);emit(id,"","session_updated",{{"title",title}});}
SessionRecord RuntimeService::archive_session(const std::string&id,bool archived){auto s=store_->archive_session(id,archived);emit(id,"","session_updated",{{"archived",archived},{"archived_at",s.archived_at}});if(archived)unregister_session_world(id);return s;}
std::string RuntimeService::generate_title(const std::string&session_id){
    auto events=store_->events_after(session_id,0);
    std::string context;
    int user_msgs=0;
    for(auto it=events.rbegin();it!=events.rend()&&user_msgs<3;++it){
        if(it->type=="message_appended"&&it->payload.value("role","")=="user"){
            context=it->payload.value("content","")+"\n"+context;
            user_msgs++;
        }
    }
    if(context.empty())return"";
    std::string prompt="Based on these user messages, generate a short title (max 20 characters) "
                       "that summarizes the conversation topic. Reply with ONLY the title, nothing else.\n\n"
                       +context;
    if(!loop_factory_)return"";
    auto loop=loop_factory_("");
    NullRunControl control;
    auto response=loop->run(prompt,control).get();
    std::string result=response.text;
    size_t start=result.find_first_not_of(" \t\n\r");
    if(start==std::string::npos)return"";
    size_t end=result.find_last_not_of(" \t\n\r");
    result=result.substr(start,end-start+1);
    if(result.size()>50)result=result.substr(0,50);
    return result;
}
std::vector<SessionRecord> RuntimeService::list_sessions(const std::string& world_id) const {
    return store_->list_sessions(world_id);
}
std::optional<SessionRecord>RuntimeService::get_session(const std::string&id)const{return store_->get_session(id);}
std::optional<RunRecord>RuntimeService::get_run(const std::string&id)const{return store_->get_run(id);}
RunRecord RuntimeService::create_run_record(const std::string&s,const std::string&m){if(!store_->get_session(s))throw RuntimeError("session_not_found","Session does not exist");if(store_->has_unfinished_run(s))throw RuntimeError("session_busy","Session already has an unfinished run");auto r=store_->create_run(s,m);emit(s,r.id,"run_started",{{"message",m}});
    auto session = store_->get_session(s);
    if (session && session->last_seq == 0 && session->title.empty()) {
        std::string auto_title = extract_title(m);
        if (!auto_title.empty()) {
            store_->update_session(s, auto_title);
        }
    }
    return r;}
RunRecord RuntimeService::start_run(const std::string&s,const std::string&m,const std::string&model){auto r=create_run_record(s,m);if(!loop_factory_)throw RuntimeError("runtime_unconfigured","Agent loop is not configured");std::thread([self=shared_from_this(),r,model]{self->execute_run(r,model);}).detach();return r;}
std::vector<AgentMetadata> RuntimeService::agents() const {
	std::vector<AgentMetadata> out;
	prompts::PromptCompositor compositor;
	prompts::PromptProfile profile;
	std::string compositor_output = compositor.assemble(profile);
	for (const auto& [id, agent] : agents_) {
		std::string description = compositor_output.empty()
			? agent.system_prompt
			: (compositor_output + "\n\n---\n\n" + agent.system_prompt);
		out.push_back({id, description});
	}
	return out;
}
DelegationStart RuntimeService::start_delegation(const std::string&s,const DelegationRequest&request){
    if(!store_->get_session(s))throw RuntimeError("session_not_found","Session does not exist");
    if(store_->has_unfinished_run(s))throw RuntimeError("session_busy","Session already has an unfinished run");
    if(request.task.empty())throw RuntimeError("invalid_request","Delegation task must not be empty");
    if(!valid_pattern(request.pattern))throw RuntimeError("invalid_pattern","Unsupported delegation pattern: "+request.pattern);
    if(!valid_aggregation(request.aggregation))throw RuntimeError("invalid_aggregation","Unsupported aggregation: "+request.aggregation);
    if(request.agent_ids.empty())throw RuntimeError("invalid_request","Delegation must include at least one agent");
    for(const auto&id:request.agent_ids)if(!agents_.contains(id))throw RuntimeError("agent_not_found","Agent not found: "+id);
    if(!sub_run_executor_)throw RuntimeError("runtime_unconfigured","Sub-agent executor is not configured");
    auto delegation_id=make_runtime_id("delegation");
    auto parent=store_->create_run(s,request.task,"",delegation_id,"","delegation");
    emit(s,parent.id,"run_started",{{"message",request.task},{"run_kind","delegation"},{"delegation_id",delegation_id}});
    std::thread([self=shared_from_this(),parent,request,delegation_id]{self->execute_delegation(parent,request,delegation_id);}).detach();
    return{delegation_id,parent.id,s};
}
prompts::PromptProfile RuntimeService::build_prompt_profile(
    const std::string& world_id, const std::string& agent_id) {

    prompts::PromptProfile profile;
    profile.category = prompts::AgentCategory::Worldbuilding;

    if (!wb_service_) return profile;

    auto agent = wb_service_->agents().get_agent(agent_id);
    if (!agent) return profile;

    int kind = static_cast<int>(agent->kind);
    profile.worldbuilding_kind = kind;
    profile.active_world_id = world_id;
    profile.active_agent_id = agent_id;

    try {
        // Find first active scene (writing or draft status) — single query, scan in-memory
        auto scenes = wb_service_->narrative().list_scenes(world_id);
        for (const auto& sc : scenes) {
            if (sc.status == "writing" || sc.status == "draft") {
                prompts::SceneContext ctx;
                ctx.scene_title = sc.title;
                ctx.world_time_label = sc.world_time;
                ctx.participant_names = sc.participant_ids;
                profile.scene_ctx = ctx;
                break;
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("build_prompt_profile: failed to load scene context for world={} agent={}: {}",
                     world_id, agent_id, e.what());
    }

    return profile;
}

void RuntimeService::execute_run(RunRecord r,std::string model){if(auto current=store_->get_run(r.id);!current||current->status==RunStatus::Cancelled)return;auto token=std::make_shared<CancellationToken>();auto control=std::make_shared<Control>(*this,r,token);{std::lock_guard lock(mutex_);tokens_[r.id]=token;controls_[r.id]=control;}store_->update_run_status(r.id,RunStatus::Running);try{std::shared_ptr<AgentLoop> loop;{std::lock_guard lock(session_loops_mutex_);auto it=session_loops_.find(r.session_id);if(it!=session_loops_.end()){loop=it->second;}else{loop=std::shared_ptr<AgentLoop>(std::move(loop_factory_(model)));session_loops_[r.session_id]=loop;auto history=restore_messages(r.session_id);if(!history.empty()){loop->restore_history(std::move(history));}}}        // Build dynamic system_prompt from WorldbuildingService when session is bound
        auto session = store_->get_session(r.session_id);
        if (session && !session->world_id.empty() && !session->agent_id.empty() && wb_service_) {
            auto profile = build_prompt_profile(session->world_id, session->agent_id);
            // Inject pipeline phase context into system prompt
            if (pipeline_mgr_ && !session->world_id.empty()) {
                auto phase_ctx = pipeline_mgr_->get_phase_context(session->world_id);
                if (!phase_ctx.empty()) {
                    profile.phase_context = std::move(phase_ctx);
                }
                profile.phase_allowed_tools = pipeline_mgr_->get_allowed_tools(session->world_id);
            }
            prompts::PromptCompositor compositor;
            std::string composed = compositor.assemble(profile);
            if (!composed.empty()) {
                loop->set_system_prompt(composed);
            }
        }
        if (session && !session->world_id.empty()) {
            loop->set_active_world_id(session->world_id);
        }
        loop->run(r.user_message,*control).get();if(token->cancelled()){if(store_->get_run(r.id)->status!=RunStatus::Cancelled){store_->update_run_status(r.id,RunStatus::Cancelled);emit(r.session_id,r.id,"run_cancelled");}}else{store_->update_run_status(r.id,RunStatus::Completed);emit(r.session_id,r.id,"run_completed",{{"pipeline_snapshot",(session&&!session->world_id.empty()&&pipeline_mgr_)?pipeline_mgr_->snapshot_to_json(session->world_id):""}});}}catch(const std::exception&e){if(token->cancelled()){if(store_->get_run(r.id)->status!=RunStatus::Cancelled){store_->update_run_status(r.id,RunStatus::Cancelled);emit(r.session_id,r.id,"run_cancelled");}}else{store_->update_run_status(r.id,RunStatus::Failed,e.what());emit(r.session_id,r.id,"run_failed",{{"error",e.what()}});}}std::lock_guard lock(mutex_);tokens_.erase(r.id);controls_.erase(r.id);}
AgentResponse RuntimeService::execute_sub_run(const SubAgentConfig& agent_, const std::string& task, const RunRecord& parent, const std::string& delegation_id, std::optional<std::string> previous_output) {
    // Enhance system_prompt with PromptCompositor output for platform agents
    SubAgentConfig agent = agent_;
    {
        prompts::PromptCompositor compositor;
        prompts::PromptProfile profile;
        std::string composed = compositor.assemble(profile);
        if (!composed.empty()) {
            agent.system_prompt = composed + "\n\n---\n\n" + agent.system_prompt;
        }
    }
    auto prompt = task;
    if(previous_output&& !previous_output->empty())prompt+="\n\nPrevious stage output:\n"+*previous_output;
    auto sub=store_->create_run(parent.session_id,prompt,parent.id,delegation_id,agent.id,"sub_run");
    std::shared_ptr<CancellationToken>token;
    {
        std::lock_guard lock(mutex_);
        auto parent_token=tokens_.find(parent.id);
        token=parent_token==tokens_.end()?std::make_shared<CancellationToken>():parent_token->second;
        child_runs_[parent.id].push_back(sub.id);
    }
    auto control=std::make_shared<Control>(*this,sub,token);
    {
        std::lock_guard lock(mutex_);
        tokens_[sub.id]=token;
        controls_[sub.id]=control;
    }
    store_->update_run_status(sub.id,RunStatus::Running);
    emit(parent.session_id,sub.id,"sub_run_started",{{"run_id",sub.id},{"parent_run_id",parent.id},{"delegation_id",delegation_id},{"agent_id",agent.id},{"task",prompt},{"status","running"}});
    try{
        if(token->cancelled())throw AgentError(ErrorType::INTERNAL_ERROR,"Run cancelled");
        auto response=sub_run_executor_(agent,prompt,*control);
        if(token->cancelled()){
            store_->update_run_status(sub.id,RunStatus::Cancelled);
            emit(parent.session_id,sub.id,"sub_run_completed",{{"run_id",sub.id},{"parent_run_id",parent.id},{"delegation_id",delegation_id},{"agent_id",agent.id},{"status","cancelled"}});
        }else{
            store_->update_run_status(sub.id,RunStatus::Completed);
            emit(parent.session_id,sub.id,"sub_run_completed",{{"run_id",sub.id},{"parent_run_id",parent.id},{"delegation_id",delegation_id},{"agent_id",agent.id},{"status","completed"},{"output_preview",response.text.substr(0,500)},{"input_tokens",response.total_input_tokens},{"output_tokens",response.total_output_tokens},{"tool_calls",response.tool_results.size()}});
        }
        std::lock_guard lock(mutex_);tokens_.erase(sub.id);controls_.erase(sub.id);
        return response;
    }catch(const std::exception&e){
        store_->update_run_status(sub.id,token->cancelled()?RunStatus::Cancelled:RunStatus::Failed,e.what());
        emit(parent.session_id,sub.id,"sub_run_completed",{{"run_id",sub.id},{"parent_run_id",parent.id},{"delegation_id",delegation_id},{"agent_id",agent.id},{"status",token->cancelled()?"cancelled":"failed"},{"error",e.what()}});
        std::lock_guard lock(mutex_);tokens_.erase(sub.id);controls_.erase(sub.id);
        AgentResponse response;response.text=e.what();return response;
    }
}
void RuntimeService::execute_delegation(RunRecord parent,DelegationRequest request,std::string delegation_id){
    auto token=std::make_shared<CancellationToken>();
    auto control=std::make_shared<Control>(*this,parent,token);
    {
        std::lock_guard lock(mutex_);
        tokens_[parent.id]=token;
        controls_[parent.id]=control;
    }
    store_->update_run_status(parent.id,RunStatus::Running);
    emit(parent.session_id,parent.id,"delegation_started",{{"delegation_id",delegation_id},{"parent_run_id",parent.id},{"pattern",request.pattern},{"agent_ids",request.agent_ids},{"aggregation",request.aggregation},{"task",request.task}});
    std::vector<AgentResponse>responses;
    try{
        if(request.pattern=="fan_out"){
            std::vector<std::future<AgentResponse>>futures;
            for(const auto&id:request.agent_ids){
                auto agent=agents_.at(id);
                futures.push_back(std::async(std::launch::async,[this,agent,task=request.task,parent,delegation_id]{return execute_sub_run(agent,task,parent,delegation_id,std::nullopt);}));
            }
            for(auto&f:futures)responses.push_back(f.get());
        }else{
            std::optional<std::string>previous;
            for(const auto&id:request.agent_ids){
                auto response=execute_sub_run(agents_.at(id),request.task,parent,delegation_id,request.pattern=="pipeline"?previous:std::nullopt);
                previous=response.text;
                responses.push_back(std::move(response));
                if(token->cancelled())break;
            }
        }
        auto output=join_agent_outputs(responses,request.agent_ids,request.aggregation);
        Message assistant;assistant.role="assistant";assistant.content=output;control->append_message(assistant);
        int input=0,output_tokens=0,tool_calls=0;bool exact=true;
        for(const auto&r:responses){input+=r.total_input_tokens;output_tokens+=r.total_output_tokens;tool_calls+=static_cast<int>(r.tool_results.size());exact=exact&&r.has_usage;}
        emit(parent.session_id,parent.id,"delegation_completed",{{"delegation_id",delegation_id},{"parent_run_id",parent.id},{"pattern",request.pattern},{"status",token->cancelled()?"cancelled":"completed"},{"aggregated_output",output},{"input_tokens",input},{"output_tokens",output_tokens},{"tool_calls",tool_calls},{"exact_usage",exact}});
        if(token->cancelled()){store_->update_run_status(parent.id,RunStatus::Cancelled);emit(parent.session_id,parent.id,"run_cancelled");}
        else{store_->update_run_status(parent.id,RunStatus::Completed);emit(parent.session_id,parent.id,"run_completed");}
    }catch(const std::exception&e){
        store_->update_run_status(parent.id,token->cancelled()?RunStatus::Cancelled:RunStatus::Failed,e.what());
        emit(parent.session_id,parent.id,token->cancelled()?"run_cancelled":"run_failed",token->cancelled()?nlohmann::json::object():nlohmann::json{{"error",e.what()}});
    }
    std::lock_guard lock(mutex_);tokens_.erase(parent.id);controls_.erase(parent.id);child_runs_.erase(parent.id);
}
ApprovalRecord RuntimeService::resolve_approval(const std::string&id,ApprovalStatus status){auto a=store_->resolve_approval(id,status);auto run=store_->get_run(a.run_id);if(!run)throw RuntimeError("run_not_found","Run does not exist");emit(run->session_id,run->id,"approval_resolved",{{"approval_id",id},{"decision",to_string(a.status)}});std::shared_ptr<Control>control;{std::lock_guard lock(mutex_);auto it=controls_.find(run->id);if(it!=controls_.end())control=it->second;}if(control)control->resolve(a.status==ApprovalStatus::Allowed);else if(run->status==RunStatus::WaitingApproval)resume_after_restarted_approval(*run,a,a.status==ApprovalStatus::Allowed);return a;}
void RuntimeService::set_worldbuilding_service(worldbuilding::WorldbuildingService* wb_service) {
    wb_service_ = wb_service;
    if (wb_service_) {
        wb_service_->set_entity_event_handler(
            [this](const std::string& event_type, const std::string& world_id,
                   const nlohmann::json& payload) {
                after_entity_event(world_id, event_type, payload);
            });
    }
}

void RuntimeService::set_skill_registry(std::shared_ptr<skills::SkillRegistry> reg) {
    skill_registry_ = std::move(reg);
}

void RuntimeService::set_pipeline_manager(
    std::shared_ptr<merak::worldbuilding::PipelineManager> mgr) {
    pipeline_mgr_ = std::move(mgr);
}

void RuntimeService::after_entity_event(const std::string& world_id,
                                         const std::string& event_type,
                                         const nlohmann::json& payload) {
    if (pipeline_mgr_) {
        pipeline_mgr_->on_world_event(world_id, event_type, payload);
    }

    // Handle scene_ended: prompt character agents to write diaries
    if (event_type == "scene_ended" && wb_service_) {
        auto scene_id = payload.value("scene_id", "");
        auto pending_agents = payload.value("pending_diary_agents",
            std::vector<std::string>{});

        for (const auto& agent_id : pending_agents) {
            auto event = emit("", "", "diary_pending", {
                {"agent_id", agent_id},
                {"scene_id", scene_id}
            });
            broadcast_to_world(world_id, event);
        }
    }
}

nlohmann::json RuntimeService::resolve_creation(
        const std::string& creation_id,
        const std::string& decision,
        const nlohmann::json& modifications) {
    if (!wb_service_) {
        throw RuntimeError("worldbuilding_not_available", "Worldbuilding service not configured");
    }

    std::string tool_name;
    auto pending = wb_service_->get_pending_creation(creation_id);
    if (pending) tool_name = pending->tool_name;

    nlohmann::json result;
    try {
        result = wb_service_->resolve_creation(creation_id, decision, modifications);
    } catch (const std::exception& e) {
        nlohmann::json error_result;
        error_result["ok"] = false;
        error_result["error"] = {{"code", "RESOLVE_FAILED"}, {"message", e.what()}};

        std::lock_guard lock(mutex_);
        for (auto& [run_id, control] : controls_) {
            if (control->awaiting_creation_id() == creation_id) {
                control->resolve_creation_result(error_result);
                auto run = store_->get_run(run_id);
                if (run) {
                    emit(run->session_id, run_id, "creation_resolved", {
                        {"creation_id", creation_id},
                        {"tool", tool_name},
                        {"decision", decision},
                        {"result", error_result}
                    });
                }
                break;
            }
        }
        throw;
    }

    {
        std::lock_guard lock(mutex_);
        for (auto& [run_id, control] : controls_) {
            if (control->awaiting_creation_id() == creation_id) {
                control->resolve_creation_result(result);
                auto run = store_->get_run(run_id);
                if (run) {
                    emit(run->session_id, run_id, "creation_resolved", {
                        {"creation_id", creation_id},
                        {"tool", tool_name},
                        {"decision", decision},
                        {"result", result}
                    });
                    if (result.value("ok", false) && pending) {
                        std::string resource_type;
                        std::string resource_id;
                        if (result.contains("scene_id")) {
                            resource_type = "scene";
                            resource_id = result.value("scene_id", "");
                        } else if (result.contains("chapter_id")) {
                            resource_type = "chapter";
                            resource_id = result.value("chapter_id", "");
                        } else if (result.contains("arc_id")) {
                            resource_type = "arc";
                            resource_id = result.value("arc_id", "");
                        } else if (result.contains("secret_id")) {
                            resource_type = "secret";
                            resource_id = result.value("secret_id", "");
                        } else if (result.contains("foreshadowing_id")) {
                            resource_type = "foreshadowing";
                            resource_id = result.value("foreshadowing_id", "");
                        } else if (result.contains("agent_id")) {
                            resource_type = "agent";
                            resource_id = result.value("agent_id", "");
                        }
                        if (!resource_type.empty() && !resource_id.empty()) {
                            emit(run->session_id, run_id, "story_context_updated", {
                                {"world_id", pending->world_id},
                                {"resource_type", resource_type},
                                {"resource_id", resource_id}
                            });
                            // Wire into PipelineManager
                            std::string event_type;
                            if (resource_type == "agent") event_type = "agent_created";
                            else if (resource_type == "scene") event_type = "scene_created";
                            else if (resource_type == "chapter") event_type = "chapter_created";
                            else if (resource_type == "secret") event_type = "secret_created";
                            else if (resource_type == "foreshadowing") event_type = "foreshadow_created";
                            else if (resource_type == "arc") event_type = "arc_created";
                            if (!event_type.empty()) {
                                after_entity_event(pending->world_id, event_type,
                                                   {{resource_type + "_id", resource_id}});
                            }
                        }
                        if (resource_type == "scene") {
                            emit(run->session_id, run_id, "scene_changed", {
                                {"scene_id", resource_id},
                                {"world_id", pending->world_id}
                            });
                        }
                    }
                }
                break;
            }
        }
    }

    return result;
}
void RuntimeService::cancel_run(const std::string&id){auto r=store_->get_run(id);if(!r)throw RuntimeError("run_not_found","Run does not exist");if(r->status==RunStatus::Completed||r->status==RunStatus::Failed||r->status==RunStatus::Cancelled)return;std::vector<std::string>children;{std::lock_guard lock(mutex_);auto child_it=child_runs_.find(id);if(child_it!=child_runs_.end())children=child_it->second;auto control=controls_.find(id);if(control!=controls_.end())control->second->cancel();else{auto token=tokens_.find(id);if(token!=tokens_.end())token->second->cancel();}for(const auto&child:children){auto child_control=controls_.find(child);if(child_control!=controls_.end())child_control->second->cancel();}}for(const auto&child:children){if(auto sub=store_->get_run(child);sub&&sub->status!=RunStatus::Completed&&sub->status!=RunStatus::Failed&&sub->status!=RunStatus::Cancelled){store_->update_run_status(child,RunStatus::Cancelled);emit(sub->session_id,child,"sub_run_completed",{{"run_id",child},{"parent_run_id",id},{"delegation_id",sub->delegation_id},{"agent_id",sub->agent_id},{"status","cancelled"}});}}store_->update_run_status(id,RunStatus::Cancelled);emit(r->session_id,id,"run_cancelled");}
void RuntimeService::respond_to_ask_user(const std::string& run_id, const std::string& call_id, const std::string& response) {
    std::lock_guard lock(mutex_);
    auto it = controls_.find(run_id);
    if (it == controls_.end()) throw RuntimeError("run_not_found", "Run does not exist");
    it->second->resolve_ask_user(call_id, response);
}
std::vector<RuntimeEvent>RuntimeService::events_after(const std::string&id,long long after)const{if(!store_->get_session(id))throw RuntimeError("session_not_found","Session does not exist");return store_->events_after(id,after);}
std::shared_ptr<EventSubscription>RuntimeService::subscribe(const std::string&id){if(!store_->get_session(id))throw RuntimeError("session_not_found","Session does not exist");return bus_.subscribe(id);}
RuntimeEvent RuntimeService::emit_event(const std::string&id,const std::string&run_id,const std::string&type,nlohmann::json payload){if(!store_->get_session(id))throw RuntimeError("session_not_found","Session does not exist");return emit(id,run_id,type,std::move(payload));}

void RuntimeService::broadcast_to_world(const std::string& world_id, RuntimeEvent event) {
    std::lock_guard lock(world_sessions_mutex_);
    auto it = world_sessions_.find(world_id);
    if (it == world_sessions_.end()) return;
    for (const auto& session_id : it->second) {
        try {
            auto e = event;
            e.session_id = session_id;
            bus_.publish(e);
        } catch (const std::exception& ex) {
            spdlog::warn("broadcast_to_world: failed for session {} in world {}: {}",
                         session_id, world_id, ex.what());
        }
    }
}
void RuntimeService::register_session_world(const std::string& session_id, const std::string& world_id) {
    std::lock_guard lock(world_sessions_mutex_);
    world_sessions_[world_id].insert(session_id);
    session_world_[session_id] = world_id;
}
void RuntimeService::unregister_session_world(const std::string& session_id) {
    std::lock_guard lock(world_sessions_mutex_);
    auto it = session_world_.find(session_id);
    if (it == session_world_.end()) return;
    const auto& world_id = it->second;
    auto wit = world_sessions_.find(world_id);
    if (wit != world_sessions_.end()) {
        wit->second.erase(session_id);
        if (wit->second.empty()) {
            world_sessions_.erase(wit);
        }
    }
    session_world_.erase(it);
}
size_t RuntimeService::world_session_count(const std::string& world_id) const {
    std::lock_guard lock(world_sessions_mutex_);
    auto it = world_sessions_.find(world_id);
    if (it == world_sessions_.end()) return 0;
    return it->second.size();
}
std::vector<Message>RuntimeService::restore_messages(const std::string&id)const{std::vector<Message>out;for(const auto&e:store_->events_after(id,0)){if(e.type=="message_appended")out.push_back(message_from_json(e.payload));else if(e.type=="compaction_applied"&&!out.empty()){auto summary=out.back();out.pop_back();auto count=std::min<size_t>(e.payload.value("replaced_count",0),out.size());out.erase(out.begin(),out.begin()+static_cast<long>(count));out.insert(out.begin(),std::move(summary));}}return out;}
void RuntimeService::resume_after_restarted_approval(RunRecord run,ApprovalRecord approval,bool allowed){
    if(!loop_factory_)throw RuntimeError("runtime_unconfigured","Agent loop is not configured");
    auto token=std::make_shared<CancellationToken>();auto control=std::make_shared<Control>(*this,run,token);
    {std::lock_guard lock(mutex_);tokens_[run.id]=token;controls_[run.id]=control;}
    store_->update_run_status(run.id,RunStatus::Running);
    // Try restoring from latest checkpoint for recovery context
    auto ckpt_json = store_->load_latest_checkpoint_json(run.id);
    if (ckpt_json.has_value()) {
        spdlog::info("Resume: found checkpoint for run {}, using for recovery context", run.id);
        try {
            auto ckpt = nlohmann::json::parse(*ckpt_json);
            spdlog::debug("Resume: checkpoint turn_index={}, turn_state={}",
                          ckpt.value("turn_index", 0), ckpt.value("turn_state", ""));
        } catch (const std::exception& e) {
            spdlog::warn("Resume: failed to parse checkpoint json: {}", e.what());
        }
    }
    auto history=restore_messages(run.session_id);
    ToolCall call{approval.tool_call_id,approval.tool_name,approval.arguments_json};ToolResult result;result.call_id=call.id;
    if(allowed){auto temp_loop=loop_factory_("");result=temp_loop->tools()->execute(call,ToolExecutionContext{token}).get();}
    else{result.is_error=true;result.output="User denied permission for tool: "+call.name;}
    control->emit_tool_completed(call,result);Message tool;tool.role="tool";tool.content=result.output;tool.tool_call_id=result.call_id;history.push_back(tool);control->append_message(tool);
    auto loop=std::shared_ptr<AgentLoop>(std::move(loop_factory_("")));loop->restore_history(std::move(history));{std::lock_guard lock(session_loops_mutex_);if(session_loops_.count(run.session_id))spdlog::warn("Resume after restart: overwriting existing session loop for {}",run.session_id);session_loops_[run.session_id]=loop;}
    std::thread([self=shared_from_this(),run,control,token,loop]()mutable{try{loop->resume(*control).get();if(token->cancelled()){self->store_->update_run_status(run.id,RunStatus::Cancelled);self->emit(run.session_id,run.id,"run_cancelled");}else{self->store_->update_run_status(run.id,RunStatus::Completed);self->emit(run.session_id,run.id,"run_completed");}}catch(const std::exception&e){self->store_->update_run_status(run.id,RunStatus::Failed,e.what());self->emit(run.session_id,run.id,"run_failed",{{"error",e.what()}});}std::lock_guard lock(self->mutex_);self->tokens_.erase(run.id);self->controls_.erase(run.id);}).detach();
}
} // namespace merak
