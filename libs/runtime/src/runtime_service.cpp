#include <merak/runtime_service.hpp>
#include <merak/turn_state.hpp>
#include <algorithm>
#include <atomic>
#include <sstream>
#include <thread>

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
        : service_(service), run_(std::move(run)), token_(std::move(token)) {}
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
    void emit_state(TurnState from,TurnState to)override{service_.emit(run_.session_id,run_.id,event_name("state_changed"),base_payload({{"from",state_name_json(from)},{"to",state_name_json(to)}}));}
    void emit_text_delta(std::string text)override{service_.emit(run_.session_id,run_.id,event_name("text_delta"),base_payload({{"text",std::move(text)}}));}
    void emit_tool_started(const ToolCall&c)override{service_.emit(run_.session_id,run_.id,event_name("tool_started"),base_payload({{"id",c.id},{"name",c.name},{"arguments",c.arguments}}));}
    void emit_tool_completed(const ToolCall&c,const ToolResult&r)override{service_.emit(run_.session_id,run_.id,event_name("tool_completed"),base_payload({{"id",c.id},{"name",c.name},{"output",r.output},{"is_error",r.is_error}}));}
    bool await_approval(const ToolCall&c)override{
        ApprovalRecord a; a.run_id=run_.id;a.tool_name=c.name;a.arguments_json=c.arguments;a.tool_call_id=c.id;
        a=service_.store_.create_approval(std::move(a));service_.store_.update_run_status(run_.id,RunStatus::WaitingApproval);
        service_.emit(run_.session_id,run_.id,"approval_requested",base_payload({{"approval_id",a.id},{"tool",c.name},{"arguments",c.arguments},{"tool_call_id",c.id}}));
        std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return decision_.has_value()||token_->cancelled();});
        if(token_->cancelled())return false;service_.store_.update_run_status(run_.id,RunStatus::Running);return *decision_;
    }
    void resolve(bool allowed){std::lock_guard lock(mutex_);decision_=allowed;changed_.notify_all();}
    void cancel(){token_->cancel();std::lock_guard lock(mutex_);changed_.notify_all();}
    void emit_usage(int in,int out,bool exact)override{service_.emit(run_.session_id,run_.id,event_name("usage_updated"),base_payload({{"input_tokens",in},{"output_tokens",out},{"exact",exact}}));}
    void append_message(const Message&m)override{service_.emit(run_.session_id,run_.id,"message_appended",message_json(m));}
    void record_compaction(int count)override{service_.emit(run_.session_id,run_.id,"compaction_applied",{{"replaced_count",count}});}
    bool cancelled()const override{return token_->cancelled();}
    std::shared_ptr<CancellationToken>cancellation_token()const override{return token_;}
private:
    RuntimeService&service_;RunRecord run_;std::shared_ptr<CancellationToken>token_;std::mutex mutex_;std::condition_variable changed_;std::optional<bool>decision_;
};

RuntimeService::RuntimeService(std::filesystem::path root,LoopFactory factory,std::map<std::string, SubAgentConfig> agents,SubRunExecutor sub_run_executor):store_(std::move(root)),loop_factory_(std::move(factory)),agents_(std::move(agents)),sub_run_executor_(std::move(sub_run_executor)){}
void RuntimeService::initialize(){store_.initialize();for(const auto&r:store_.interrupt_running_runs())emit(r.session_id,r.id,"run_interrupted",{{"reason","server restarted"}});}
RuntimeEvent RuntimeService::emit(const std::string&s,const std::string&r,const std::string&t,nlohmann::json p){RuntimeEvent e{0,"",s,r,t,std::move(p)};e=store_.append_event(std::move(e));bus_.publish(e);return e;}
SessionRecord RuntimeService::create_session(const std::string&title){auto s=store_.create_session(title);emit(s.id,"","session_created",{{"title",title}});return *store_.get_session(s.id);}
std::vector<SessionRecord>RuntimeService::list_sessions()const{return store_.list_sessions();}
std::optional<SessionRecord>RuntimeService::get_session(const std::string&id)const{return store_.get_session(id);}
std::optional<RunRecord>RuntimeService::get_run(const std::string&id)const{return store_.get_run(id);}
RunRecord RuntimeService::create_run_record(const std::string&s,const std::string&m){if(!store_.get_session(s))throw RuntimeError("session_not_found","Session does not exist");if(store_.has_unfinished_run(s))throw RuntimeError("session_busy","Session already has an unfinished run");auto r=store_.create_run(s,m);emit(s,r.id,"run_started",{{"message",m}});return r;}
RunRecord RuntimeService::start_run(const std::string&s,const std::string&m){auto r=create_run_record(s,m);if(!loop_factory_)throw RuntimeError("runtime_unconfigured","Agent loop is not configured");std::thread([self=shared_from_this(),r]{self->execute_run(r);}).detach();return r;}
std::vector<AgentMetadata>RuntimeService::agents()const{std::vector<AgentMetadata>out;for(const auto&[id,agent]:agents_)out.push_back({id,agent.system_prompt});return out;}
DelegationStart RuntimeService::start_delegation(const std::string&s,const DelegationRequest&request){
    if(!store_.get_session(s))throw RuntimeError("session_not_found","Session does not exist");
    if(store_.has_unfinished_run(s))throw RuntimeError("session_busy","Session already has an unfinished run");
    if(request.task.empty())throw RuntimeError("invalid_request","Delegation task must not be empty");
    if(!valid_pattern(request.pattern))throw RuntimeError("invalid_pattern","Unsupported delegation pattern: "+request.pattern);
    if(!valid_aggregation(request.aggregation))throw RuntimeError("invalid_aggregation","Unsupported aggregation: "+request.aggregation);
    if(request.agent_ids.empty())throw RuntimeError("invalid_request","Delegation must include at least one agent");
    for(const auto&id:request.agent_ids)if(!agents_.contains(id))throw RuntimeError("agent_not_found","Agent not found: "+id);
    if(!sub_run_executor_)throw RuntimeError("runtime_unconfigured","Sub-agent executor is not configured");
    auto delegation_id=make_runtime_id("delegation");
    auto parent=store_.create_run(s,request.task,"",delegation_id,"","delegation");
    emit(s,parent.id,"run_started",{{"message",request.task},{"run_kind","delegation"},{"delegation_id",delegation_id}});
    std::thread([self=shared_from_this(),parent,request,delegation_id]{self->execute_delegation(parent,request,delegation_id);}).detach();
    return{delegation_id,parent.id,s};
}
void RuntimeService::execute_run(RunRecord r){if(auto current=store_.get_run(r.id);!current||current->status==RunStatus::Cancelled)return;auto token=std::make_shared<CancellationToken>();auto control=std::make_shared<Control>(*this,r,token);{std::lock_guard lock(mutex_);tokens_[r.id]=token;controls_[r.id]=control;}store_.update_run_status(r.id,RunStatus::Running);try{auto loop=loop_factory_();loop->run(r.user_message,*control).get();if(token->cancelled()){if(store_.get_run(r.id)->status!=RunStatus::Cancelled){store_.update_run_status(r.id,RunStatus::Cancelled);emit(r.session_id,r.id,"run_cancelled");}}else{store_.update_run_status(r.id,RunStatus::Completed);emit(r.session_id,r.id,"run_completed");}}catch(const std::exception&e){if(token->cancelled()){if(store_.get_run(r.id)->status!=RunStatus::Cancelled){store_.update_run_status(r.id,RunStatus::Cancelled);emit(r.session_id,r.id,"run_cancelled");}}else{store_.update_run_status(r.id,RunStatus::Failed,e.what());emit(r.session_id,r.id,"run_failed",{{"error",e.what()}});}}std::lock_guard lock(mutex_);tokens_.erase(r.id);controls_.erase(r.id);}
AgentResponse RuntimeService::execute_sub_run(const SubAgentConfig&agent,const std::string&task,const RunRecord&parent,const std::string&delegation_id,std::optional<std::string>previous_output){
    auto prompt=task;
    if(previous_output&& !previous_output->empty())prompt+="\n\nPrevious stage output:\n"+*previous_output;
    auto sub=store_.create_run(parent.session_id,prompt,parent.id,delegation_id,agent.id,"sub_run");
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
    store_.update_run_status(sub.id,RunStatus::Running);
    emit(parent.session_id,sub.id,"sub_run_started",{{"run_id",sub.id},{"parent_run_id",parent.id},{"delegation_id",delegation_id},{"agent_id",agent.id},{"task",prompt},{"status","running"}});
    try{
        if(token->cancelled())throw AgentError(ErrorType::INTERNAL_ERROR,"Run cancelled");
        auto response=sub_run_executor_(agent,prompt,*control);
        if(token->cancelled()){
            store_.update_run_status(sub.id,RunStatus::Cancelled);
            emit(parent.session_id,sub.id,"sub_run_completed",{{"run_id",sub.id},{"parent_run_id",parent.id},{"delegation_id",delegation_id},{"agent_id",agent.id},{"status","cancelled"}});
        }else{
            store_.update_run_status(sub.id,RunStatus::Completed);
            emit(parent.session_id,sub.id,"sub_run_completed",{{"run_id",sub.id},{"parent_run_id",parent.id},{"delegation_id",delegation_id},{"agent_id",agent.id},{"status","completed"},{"output_preview",response.text.substr(0,500)},{"input_tokens",response.total_input_tokens},{"output_tokens",response.total_output_tokens},{"tool_calls",response.tool_results.size()}});
        }
        std::lock_guard lock(mutex_);tokens_.erase(sub.id);controls_.erase(sub.id);
        return response;
    }catch(const std::exception&e){
        store_.update_run_status(sub.id,token->cancelled()?RunStatus::Cancelled:RunStatus::Failed,e.what());
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
    store_.update_run_status(parent.id,RunStatus::Running);
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
        if(token->cancelled()){store_.update_run_status(parent.id,RunStatus::Cancelled);emit(parent.session_id,parent.id,"run_cancelled");}
        else{store_.update_run_status(parent.id,RunStatus::Completed);emit(parent.session_id,parent.id,"run_completed");}
    }catch(const std::exception&e){
        store_.update_run_status(parent.id,token->cancelled()?RunStatus::Cancelled:RunStatus::Failed,e.what());
        emit(parent.session_id,parent.id,token->cancelled()?"run_cancelled":"run_failed",token->cancelled()?nlohmann::json::object():nlohmann::json{{"error",e.what()}});
    }
    std::lock_guard lock(mutex_);tokens_.erase(parent.id);controls_.erase(parent.id);child_runs_.erase(parent.id);
}
ApprovalRecord RuntimeService::resolve_approval(const std::string&id,ApprovalStatus status){auto a=store_.resolve_approval(id,status);auto run=store_.get_run(a.run_id);if(!run)throw RuntimeError("run_not_found","Run does not exist");emit(run->session_id,run->id,"approval_resolved",{{"approval_id",id},{"decision",to_string(a.status)}});std::shared_ptr<Control>control;{std::lock_guard lock(mutex_);auto it=controls_.find(run->id);if(it!=controls_.end())control=it->second;}if(control)control->resolve(a.status==ApprovalStatus::Allowed);else if(run->status==RunStatus::WaitingApproval)resume_after_restarted_approval(*run,a,a.status==ApprovalStatus::Allowed);return a;}
void RuntimeService::cancel_run(const std::string&id){auto r=store_.get_run(id);if(!r)throw RuntimeError("run_not_found","Run does not exist");if(r->status==RunStatus::Completed||r->status==RunStatus::Failed||r->status==RunStatus::Cancelled)return;std::vector<std::string>children;{std::lock_guard lock(mutex_);auto child_it=child_runs_.find(id);if(child_it!=child_runs_.end())children=child_it->second;auto control=controls_.find(id);if(control!=controls_.end())control->second->cancel();else{auto token=tokens_.find(id);if(token!=tokens_.end())token->second->cancel();}for(const auto&child:children){auto child_control=controls_.find(child);if(child_control!=controls_.end())child_control->second->cancel();}}for(const auto&child:children){if(auto sub=store_.get_run(child);sub&&sub->status!=RunStatus::Completed&&sub->status!=RunStatus::Failed&&sub->status!=RunStatus::Cancelled){store_.update_run_status(child,RunStatus::Cancelled);emit(sub->session_id,child,"sub_run_completed",{{"run_id",child},{"parent_run_id",id},{"delegation_id",sub->delegation_id},{"agent_id",sub->agent_id},{"status","cancelled"}});}}store_.update_run_status(id,RunStatus::Cancelled);emit(r->session_id,id,"run_cancelled");}
std::vector<RuntimeEvent>RuntimeService::events_after(const std::string&id,long long after)const{if(!store_.get_session(id))throw RuntimeError("session_not_found","Session does not exist");return store_.events_after(id,after);}
std::shared_ptr<EventSubscription>RuntimeService::subscribe(const std::string&id){if(!store_.get_session(id))throw RuntimeError("session_not_found","Session does not exist");return bus_.subscribe(id);}

std::vector<Message>RuntimeService::restore_messages(const std::string&id)const{std::vector<Message>out;for(const auto&e:store_.events_after(id,0)){if(e.type=="message_appended")out.push_back(message_from_json(e.payload));else if(e.type=="compaction_applied"&&!out.empty()){auto summary=out.back();out.pop_back();auto count=std::min<size_t>(e.payload.value("replaced_count",0),out.size());out.erase(out.begin(),out.begin()+static_cast<long>(count));out.insert(out.begin(),std::move(summary));}}return out;}
void RuntimeService::resume_after_restarted_approval(RunRecord run,ApprovalRecord approval,bool allowed){
    if(!loop_factory_)throw RuntimeError("runtime_unconfigured","Agent loop is not configured");
    auto token=std::make_shared<CancellationToken>();auto control=std::make_shared<Control>(*this,run,token);
    {std::lock_guard lock(mutex_);tokens_[run.id]=token;controls_[run.id]=control;}
    store_.update_run_status(run.id,RunStatus::Running);auto history=restore_messages(run.session_id);
    ToolCall call{approval.tool_call_id,approval.tool_name,approval.arguments_json};ToolResult result;result.call_id=call.id;
    if(allowed){auto loop=loop_factory_();result=loop->tools()->execute(call,ToolExecutionContext{token}).get();}
    else{result.is_error=true;result.output="User denied permission for tool: "+call.name;}
    control->emit_tool_completed(call,result);Message tool;tool.role="tool";tool.content=result.output;tool.tool_call_id=result.call_id;history.push_back(tool);control->append_message(tool);
    std::thread([self=shared_from_this(),run,control,token,history=std::move(history)]()mutable{try{auto loop=self->loop_factory_();loop->run("",*control,std::move(history),false).get();if(token->cancelled()){self->store_.update_run_status(run.id,RunStatus::Cancelled);self->emit(run.session_id,run.id,"run_cancelled");}else{self->store_.update_run_status(run.id,RunStatus::Completed);self->emit(run.session_id,run.id,"run_completed");}}catch(const std::exception&e){self->store_.update_run_status(run.id,RunStatus::Failed,e.what());self->emit(run.session_id,run.id,"run_failed",{{"error",e.what()}});}std::lock_guard lock(self->mutex_);self->tokens_.erase(run.id);self->controls_.erase(run.id);}).detach();
}
} // namespace merak
