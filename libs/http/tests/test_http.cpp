#include <merak/http_server.hpp>
#include <cassert>
#include <filesystem>

using namespace merak;

int main() {
    auto root = std::filesystem::temp_directory_path() / "merak-http-test";
    std::filesystem::remove_all(root);
    auto runtime = std::make_shared<RuntimeService>(root);
    runtime->initialize();
    HttpServer server(runtime, RuntimeMetadata{"test", "fake-model", {}, {}});

    auto created = server.handle_create_session();
    assert(created.status == 201);
    assert(created.body.contains("session_id"));

    auto metadata = server.handle_runtime_metadata();
    assert(metadata.status == 200);
    assert(metadata.body["provider"] == "test");

    auto missing = server.handle_get_session("missing");
    assert(missing.status == 404);
    assert(missing.body["error"]["code"] == "session_not_found");
    std::filesystem::remove_all(root);
}
