#include <merak/http_server.hpp>
#include <cassert>
#include <filesystem>

using namespace merak;

int main() {
    auto root = std::filesystem::temp_directory_path() / "merak-http-test";
    std::filesystem::remove_all(root);
    std::map<std::string, SubAgentConfig> agents;
    SubAgentConfig researcher;
    researcher.id = "researcher";
    researcher.system_prompt = "Research agent";
    agents[researcher.id] = researcher;
    auto runtime = std::make_shared<RuntimeService>(root, RuntimeService::LoopFactory{}, agents);
    runtime->initialize();
    RuntimeMetadata meta;
    meta.provider = "test";
    meta.model = "fake-model";
    meta.permission_mode = "ask";
    meta.agents.push_back({"researcher", "Research agent"});
    HttpServer server(runtime, meta);

    auto created = server.handle_create_session();
    assert(created.status == 201);
    assert(created.body.contains("session_id"));

    auto metadata = server.handle_runtime_metadata();
    assert(metadata.status == 200);
    assert(metadata.body["provider"] == "test");
    assert(metadata.body["models"].size() == 1);
    assert(metadata.body["models"][0]["name"] == "fake-model");
    assert(metadata.body["permission_mode"] == "ask");
    assert(metadata.body["memory"]["enabled"] == false);
    assert(metadata.body["delegation_patterns"].size() == 3);
    assert(metadata.body["delegation_patterns"][0] == "fan_out");
    assert(metadata.body["agents"].size() == 1);
    assert(metadata.body["agents"][0]["id"] == "researcher");

    auto memory = server.handle_session_memory(created.body["session_id"].get<std::string>());
    assert(memory.status == 200);
    assert(memory.body["items"].is_array());

    DelegationRequest request;
    request.pattern = "fan_out";
    request.agent_ids = {"researcher"};
    request.task = "inspect";
    auto delegation_result = server.handle_create_delegation(
        created.body["session_id"].get<std::string>(), request);
    assert(delegation_result.status == 202 || delegation_result.status == 400);
    if (delegation_result.status == 400) {
        assert(delegation_result.body["error"]["code"] == "runtime_unconfigured");
    }

    auto missing = server.handle_get_session("missing");
    assert(missing.status == 404);
    assert(missing.body["error"]["code"] == "session_not_found");
    std::filesystem::remove_all(root);
}
