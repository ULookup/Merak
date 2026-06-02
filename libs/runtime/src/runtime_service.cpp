#include <merak/runtime_service.hpp>
#include <merak/turn_state.hpp>
#include <algorithm>
#include <thread>

namespace merak {
namespace {
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
    void emit_state(TurnState from,TurnState to)override{service_.emit(run_.session_id,run_.id,"state_changed",{{"from",state_name_json(from)},{"to",state_name_json(to)}});}
    void emit_text_delta(std::string text)override{service_.emit(run_.session_id,run_.id,"text_delta",{{"text",std::move(text)}});}
    void emit_tool_started(const ToolCall&c)override{service_.emit(run_.session_id,run_.id,"tool_started",{{"id",c.id},{"name",c.name},{"arguments",c.arguments}});}
    void emit_tool_completed(const ToolCall&c,const ToolResult&r)override{service_.emit(run_.session_id,run_.id,"tool_completed",{{"id",c.id},{"name",c.name},{"output",r.output},{"is_error",r.is_error}});}
    bool await_approval(const ToolCall&c)override{
        ApprovalRecord a; a.run_id=run_.id;a.tool_name=c.name;a.arguments_json=c.arguments;a.tool_call_id=c.id;
        a=service_.store_.create_approval(std::move(a));service_.store_.update_run_status(run_.id,RunStatus::WaitingApproval);
        service_.emit(run_.session_id,run_.id,"approval_requested",{{"approval_id",a.id},{"tool",c.name},{"arguments",c.arguments},{"tool_call_id",c.id}});
        std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return decision_.has_value()||token_->cancelled();});
        if(token_->cancelled())return false;service_.store_.update_run_status(run_.id,RunStatus::Running);return *decision_;
    }
    void resolve(bool allowed){std::lock_guard lock(mutex_);decision_=allowed;changed_.notify_all();}
    void cancel(){token_->cancel();std::lock_guard lock(mutex_);changed_.notify_all();}
    void emit_usage(int in,int out,bool exact)override{service_.emit(run_.session_id,run_.id,"usage_updated",{{"input_tokens",in},{"output_tokens",out},{"exact",exact}});}
    void append_message(const Message&m)override{service_.emit(run_.session_id,run_.id,"message_appended",message_json(m));}
    void record_compaction(int count)override{service_.emit(run_.session_id,run_.id,"compaction_applied",{{"replaced_count",count}});}
    bool cancelled()const override{return token_->cancelled();}
    std::shared_ptr<CancellationToken>cancellation_token()const override{return token_;}
private:
    RuntimeService&service_;RunRecord run_;std::shared_ptr<CancellationToken>token_;std::mutex mutex_;std::condition_variable changed_;std::optional<bool>decision_;
};

RuntimeService::RuntimeService(std::filesystem::path root,LoopFactory factory):store_(std::move(root)),loop_factory_(std::move(factory)){}
void RuntimeService::initialize(){store_.initialize();for(const auto&r:store_.interrupt_running_runs())emit(r.session_id,r.id,"run_interrupted",{{"reason","server restarted"}});}
RuntimeEvent RuntimeService::emit(const std::string&s,const std::string&r,const std::string&t,nlohmann::json p){RuntimeEvent e{0,"",s,r,t,std::move(p)};e=store_.append_event(std::move(e));bus_.publish(e);return e;}
SessionRecord RuntimeService::create_session(const std::string&title){auto s=store_.create_session(title);emit(s.id,"","session_created",{{"title",title}});return *store_.get_session(s.id);}
std::vector<SessionRecord>RuntimeService::list_sessions()const{return store_.list_sessions();}
std::optional<SessionRecord>RuntimeService::get_session(const std::string&id)const{return store_.get_session(id);}
std::optional<RunRecord>RuntimeService::get_run(const std::string&id)const{return store_.get_run(id);}
RunRecord RuntimeService::create_run_record(const std::string&s,const std::string&m){if(!store_.get_session(s))throw RuntimeError("session_not_found","Session does not exist");if(store_.has_unfinished_run(s))throw RuntimeError("session_busy","Session already has an unfinished run");auto r=store_.create_run(s,m);emit(s,r.id,"run_started",{{"message",m}});return r;}
RunRecord RuntimeService::start_run(const std::string&s,const std::string&m){auto r=create_run_record(s,m);if(!loop_factory_)throw RuntimeError("runtime_unconfigured","Agent loop is not configured");std::thread([self=shared_from_this(),r]{self->execute_run(r);}).detach();return r;}
void RuntimeService::execute_run(RunRecord r){if(auto current=store_.get_run(r.id);!current||current->status==RunStatus::Cancelled)return;auto token=std::make_shared<CancellationToken>();auto control=std::make_shared<Control>(*this,r,token);{std::lock_guard lock(mutex_);tokens_[r.id]=token;controls_[r.id]=control;}store_.update_run_status(r.id,RunStatus::Running);try{auto loop=loop_factory_();loop->run(r.user_message,*control).get();if(token->cancelled()){if(store_.get_run(r.id)->status!=RunStatus::Cancelled){store_.update_run_status(r.id,RunStatus::Cancelled);emit(r.session_id,r.id,"run_cancelled");}}else{store_.update_run_status(r.id,RunStatus::Completed);emit(r.session_id,r.id,"run_completed");}}catch(const std::exception&e){if(token->cancelled()){if(store_.get_run(r.id)->status!=RunStatus::Cancelled){store_.update_run_status(r.id,RunStatus::Cancelled);emit(r.session_id,r.id,"run_cancelled");}}else{store_.update_run_status(r.id,RunStatus::Failed,e.what());emit(r.session_id,r.id,"run_failed",{{"error",e.what()}});}}std::lock_guard lock(mutex_);tokens_.erase(r.id);controls_.erase(r.id);}
ApprovalRecord RuntimeService::resolve_approval(const std::string&id,ApprovalStatus status){auto a=store_.resolve_approval(id,status);auto run=store_.get_run(a.run_id);if(!run)throw RuntimeError("run_not_found","Run does not exist");emit(run->session_id,run->id,"approval_resolved",{{"approval_id",id},{"decision",to_string(a.status)}});std::shared_ptr<Control>control;{std::lock_guard lock(mutex_);auto it=controls_.find(run->id);if(it!=controls_.end())control=it->second;}if(control)control->resolve(a.status==ApprovalStatus::Allowed);else if(run->status==RunStatus::WaitingApproval)resume_after_restarted_approval(*run,a,a.status==ApprovalStatus::Allowed);return a;}
void RuntimeService::cancel_run(const std::string&id){auto r=store_.get_run(id);if(!r)throw RuntimeError("run_not_found","Run does not exist");if(r->status==RunStatus::Completed||r->status==RunStatus::Failed||r->status==RunStatus::Cancelled)return;{std::lock_guard lock(mutex_);auto control=controls_.find(id);if(control!=controls_.end())control->second->cancel();else{auto token=tokens_.find(id);if(token!=tokens_.end())token->second->cancel();}}store_.update_run_status(id,RunStatus::Cancelled);emit(r->session_id,id,"run_cancelled");}
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
