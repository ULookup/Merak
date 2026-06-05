#include "client/runtime_client.hpp"
#include <cassert>

using namespace merak::client;

int main() {
    auto frame = parse_sse_frame(
        "id: 7\n"
        "event: text_delta\n"
        "data: {\"text\":\"hello\"}\n\n");
    assert(frame.has_value());
    assert(frame->seq == 7);
    assert(frame->type == "text_delta");
    assert(frame->payload["text"] == "hello");

    auto url = events_stream_url("http://127.0.0.1:3888", "session_1", 9);
    assert(url == "http://127.0.0.1:3888/v1/sessions/session_1/events/stream?after=9");

    assert(delegations_path("session_1") == "/v1/sessions/session_1/delegations");
}
