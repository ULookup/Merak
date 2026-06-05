#include <merak/anthropic_provider.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <future>

namespace merak {

AnthropicProvider::AnthropicProvider(const LLMConfig& config)
    : config_(config)
{
    spdlog::info("AnthropicProvider initialized: model={}", config_.default_model);
}

AnthropicProvider::~AnthropicProvider() = default;

nlohmann::json AnthropicProvider::build_request_body(const ChatRequest& request) const {
    nlohmann::json body;
    body["model"] = request.model;
    body["max_tokens"] = request.max_output_tokens;
    body["stream"] = true;

    if (request.enable_thinking &&
        config_.thinking.has_value() &&
        config_.thinking->type != "disabled") {
        const auto& thinking = *config_.thinking;
        body["thinking"]["type"] = thinking.type;
        if (thinking.type == "enabled") {
            body["thinking"]["budget_tokens"] = thinking.budget_tokens;
        }
        if (!thinking.effort.empty()) {
            body["output_config"]["effort"] = thinking.effort;
        }
    }

    // Anthropic 的 system prompt 是顶层字段，不在 messages 数组里
    nlohmann::json messages = nlohmann::json::array();
    for (auto& m : request.messages) {
        if (m.role == "system") {
            body["system"] = m.content;
            continue;
        }

        nlohmann::json item;
        item["role"] = m.role;

        // assistant 消息可能包含 tool_use content blocks
        if (m.role == "assistant" && !m.tool_calls.empty()) {
            nlohmann::json content_arr = nlohmann::json::array();
            if (!m.provider_content_blocks_json.empty()) {
                auto preserved_blocks = nlohmann::json::parse(
                    m.provider_content_blocks_json);
                for (auto& block : preserved_blocks) {
                    content_arr.push_back(block);
                }
            }
            if (!m.content.empty()) {
                nlohmann::json text_block;
                text_block["type"] = "text";
                text_block["text"] = m.content;
                content_arr.push_back(text_block);
            }
            for (auto& tc : m.tool_calls) {
                nlohmann::json tool_block;
                tool_block["type"] = "tool_use";
                tool_block["id"] = tc.id;
                tool_block["name"] = tc.name;
                tool_block["input"] = nlohmann::json::parse(
                    tc.arguments.empty() ? "{}" : tc.arguments);
                content_arr.push_back(tool_block);
            }
            item["content"] = content_arr;
        }
        // tool 消息 — Anthropic 格式不同
        else if (m.role == "tool") {
            item["role"] = "user";
            nlohmann::json content_arr = nlohmann::json::array();
            nlohmann::json tool_result;
            tool_result["type"] = "tool_result";
            tool_result["tool_use_id"] = m.tool_call_id.value_or("");
            tool_result["content"] = m.content;
            content_arr.push_back(tool_result);
            item["content"] = content_arr;
        }
        else {
            item["content"] = m.content;
        }

        messages.push_back(item);
    }
    body["messages"] = messages;

    // 工具定义 — Anthropic 格式
    if (!request.tools.empty()) {
        nlohmann::json tools_arr = nlohmann::json::array();
        for (auto& t : request.tools) {
            nlohmann::json tool;
            tool["name"] = t.name;
            tool["description"] = t.description;
            if (!t.parameters_json.empty()) {
                tool["input_schema"] = nlohmann::json::parse(t.parameters_json);
            } else {
                tool["input_schema"]["type"] = "object";
                tool["input_schema"]["properties"] = nlohmann::json::object();
            }
            tools_arr.push_back(tool);
        }
        body["tools"] = tools_arr;
    }

    return body;
}

std::future<AgentResponse> AnthropicProvider::chat(
    const ChatRequest& request,
    std::function<void(StreamChunk)> on_chunk,
    std::shared_ptr<CancellationToken> cancellation
) {
    return std::async(std::launch::async, [this, request, on_chunk, cancellation]() -> AgentResponse {
        nlohmann::json body = build_request_body(request);
        std::string url = config_.api_base_url + "/messages";
        std::string body_str = body.dump();
        spdlog::debug("Anthropic request: url={}, body_size={}", url, body_str.size());

        CURL* curl = curl_easy_init();
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers,
            ("x-api-key: " + config_.api_key).c_str());
        headers = curl_slist_append(headers,
            "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
            +[](void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                auto* token = static_cast<CancellationToken*>(userdata);
                return token && token->cancelled() ? 1 : 0;
            });
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancellation.get());

        // SSE 累积状态
        std::string response_text;
        int input_tokens = 0, output_tokens = 0;
        bool has_usage = false;
        struct PendingTool {
            std::string id;
            std::string name;
            std::string arguments_json; // 增量累积
        };
        std::map<int, PendingTool> pending_tools; // index → tool
        std::map<int, nlohmann::json> preserved_content_blocks;
        std::vector<ToolCall> accumulated_tool_calls;
        std::string current_event;
        std::string current_data;

        // Anthropic SSE 回调 — 每行处理
        std::string line_buffer;
        auto process_event = [&](const std::string& event_type, const std::string& data) {
            if (data.empty()) return;
            try {
                auto j = nlohmann::json::parse(data);

                if (event_type == "message_start") {
                    if (j.contains("message") && j["message"].contains("usage")) {
                        input_tokens = j["message"]["usage"].value("input_tokens", 0);
                        has_usage = true;
                    }
                }
                else if (event_type == "content_block_start") {
                    auto& block = j["content_block"];
                    int idx = j.value("index", -1);
                    if (block.value("type", "") == "tool_use") {
                        PendingTool pt;
                        pt.id = block.value("id", "");
                        pt.name = block.value("name", "");
                        pending_tools[idx] = std::move(pt);
                    } else if (block.value("type", "") == "thinking" ||
                               block.value("type", "") == "redacted_thinking") {
                        preserved_content_blocks[idx] = block;
                    }
                }
                else if (event_type == "content_block_delta") {
                    auto& delta = j["delta"];
                    std::string delta_type = delta.value("type", "");
                    if (delta_type == "text_delta") {
                        std::string text = delta.value("text", "");
                        response_text += text;
                        StreamChunk chunk{text, false, false};
                        on_chunk(chunk);
                    } else if (delta_type == "input_json_delta") {
                        int idx = j.value("index", -1);
                        if (pending_tools.count(idx)) {
                            pending_tools[idx].arguments_json +=
                                delta.value("partial_json", "");
                        }
                    } else if (delta_type == "thinking_delta") {
                        int idx = j.value("index", -1);
                        if (preserved_content_blocks.count(idx)) {
                            preserved_content_blocks[idx]["thinking"] =
                                preserved_content_blocks[idx].value("thinking", "") +
                                delta.value("thinking", "");
                        }
                    } else if (delta_type == "signature_delta") {
                        int idx = j.value("index", -1);
                        if (preserved_content_blocks.count(idx)) {
                            preserved_content_blocks[idx]["signature"] =
                                delta.value("signature", "");
                        }
                    }
                }
                else if (event_type == "content_block_stop") {
                    int idx = j.value("index", -1);
                    auto it = pending_tools.find(idx);
                    if (it != pending_tools.end()) {
                        ToolCall tc;
                        tc.id = it->second.id;
                        tc.name = it->second.name;
                        tc.arguments = it->second.arguments_json;
                        accumulated_tool_calls.push_back(std::move(tc));
                        pending_tools.erase(it);
                    }
                }
                else if (event_type == "message_delta") {
                    if (j.contains("usage")) {
                        output_tokens = j["usage"].value("output_tokens", 0);
                        has_usage = true;
                    }
                }
                else if (event_type == "message_stop") {
                    StreamChunk chunk{"", true, false};
                    on_chunk(chunk);
                }
            } catch (const nlohmann::json::exception& e) {
                spdlog::warn("Anthropic SSE parse error: {}", e.what());
            }
        };

        auto write_callback = [&](const std::string& data) {
            line_buffer += data;
            size_t newline = 0;
            while ((newline = line_buffer.find('\n')) != std::string::npos) {
                std::string line = line_buffer.substr(0, newline);
                line_buffer.erase(0, newline + 1);
                // 去掉行尾的 \r
                if (!line.empty() && line.back() == '\r') line.pop_back();

                if (line.rfind("event: ", 0) == 0) {
                    current_event = line.substr(7);
                } else if (line.rfind("data: ", 0) == 0) {
                    current_data = line.substr(6);
                } else if (line.empty()) {
                    // 空行 = 事件结束
                    if (!current_data.empty()) {
                        process_event(current_event, current_data);
                    }
                    current_event.clear();
                    current_data.clear();
                }
            }
        };

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
            +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                auto* cb = static_cast<decltype(&write_callback)>(userdata);
                std::string data(ptr, size * nmemb);
                (*cb)(data);
                return size * nmemb;
            });

        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_callback);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            spdlog::error("curl error: {}", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            throw AgentError(
                cancellation && cancellation->cancelled()
                    ? ErrorType::LLM_TIMEOUT : ErrorType::LLM_ERROR,
                cancellation && cancellation->cancelled()
                    ? "LLM request cancelled" : curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        AgentResponse response;
        response.tool_calls = std::move(accumulated_tool_calls);
        response.text = response_text;
        response.total_input_tokens = input_tokens;
        response.total_output_tokens = output_tokens;
        response.has_usage = has_usage;
        if (!preserved_content_blocks.empty()) {
            auto blocks = nlohmann::json::array();
            for (auto& [_, block] : preserved_content_blocks) {
                blocks.push_back(block);
            }
            response.provider_content_blocks_json = blocks.dump();
        }

        stats_.total_requests++;
        return response;
    });
}

} // namespace merak
