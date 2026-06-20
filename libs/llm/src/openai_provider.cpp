#include <merak/openai_provider.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <future>
#include <chrono>
#include <thread>

namespace merak {

OpenAIProvider::OpenAIProvider(const LLMConfig& config)
    : config_(config)
{
    spdlog::info("OpenAIProvider initialized: base_url={}, model={}",
        config_.api_base_url, config_.default_model);
}

OpenAIProvider::~OpenAIProvider() = default;

std::future<AgentResponse> OpenAIProvider::chat(
    const ChatRequest& request,
    std::function<void(StreamChunk)> on_chunk,
    std::shared_ptr<CancellationToken> cancellation
) {
    return std::async(std::launch::async, [this, request, on_chunk, cancellation]() -> AgentResponse {
        nlohmann::json body;
        body["model"] = request.model;
        body["messages"] = build_messages(request.messages);
        body["stream"] = true;
        body["stream_options"]["include_usage"] = true;
        body["max_tokens"] = request.max_output_tokens;

        if (!request.tools.empty()) {
            body["tools"] = build_tools(request.tools);
        }

        std::string url = config_.api_base_url + "/chat/completions";
        std::string body_str = body.dump();
        spdlog::debug("OpenAI request: url={}, body_size={}", url, body_str.size());

        std::string response_text;
        int input_tokens = 0, output_tokens = 0;
        bool has_usage = false;
        nlohmann::json accumulated_tool_calls_json = nlohmann::json::array();
        std::string line_buffer;

        auto write_callback = [&](const std::string& data) {
            line_buffer += data;
            size_t newline = 0;
            while ((newline = line_buffer.find('\n')) != std::string::npos) {
                std::string line = line_buffer.substr(0, newline);
                line_buffer.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("data: ", 0) != 0) continue;
                std::string json_line = line.substr(6);
                if (json_line == "[DONE]") {
                    StreamChunk chunk{"", true, false};
                    on_chunk(chunk);
                    continue;
                }

                try {
                    auto j = nlohmann::json::parse(json_line);
                    auto& choices = j["choices"];
                    if (!choices.empty()) {
                        auto& delta = choices[0]["delta"];

                        if (delta.contains("content") && !delta["content"].is_null()) {
                            std::string text = delta["content"].get<std::string>();
                            response_text += text;
                            StreamChunk chunk{text, false, false};
                            on_chunk(chunk);
                        }

                        // 工具调用 delta 累积
                        if (delta.contains("tool_calls")) {
                            for (auto& tc : delta["tool_calls"]) {
                                int idx = tc.value("index", 0);
                                while (idx >= (int)accumulated_tool_calls_json.size()) {
                                    accumulated_tool_calls_json.push_back(
                                        nlohmann::json::object());
                                }
                                auto& acc = accumulated_tool_calls_json[idx];
                                if (tc.contains("id") && !tc["id"].is_null()) {
                                    acc["id"] = tc["id"];
                                }
                                if (tc.contains("function")) {
                                    auto& func = tc["function"];
                                    if (func.contains("name") && !func["name"].is_null()) {
                                        acc["function"]["name"] = func["name"];
                                    }
                                    if (func.contains("arguments")) {
                                        std::string prev = acc["function"].value("arguments", "");
                                        acc["function"]["arguments"] = prev +
                                            func["arguments"].get<std::string>();
                                    }
                                }
                            }
                        }
                    }

                    if (j.contains("usage") && !j["usage"].is_null()) {
                        input_tokens = j["usage"].value("prompt_tokens", 0);
                        output_tokens = j["usage"].value("completion_tokens", 0);
                        has_usage = true;
                        auto& details = j["usage"]["prompt_tokens_details"];
                        if (!details.is_null()) {
                            int cached = details.value("cached_tokens", 0);
                            if (cached > 0) {
                                stats_.cache_hits++;
                                stats_.cache_read_tokens += cached;
                            }
                        }
                    }
                } catch (const nlohmann::json::exception& e) {
                    spdlog::warn("SSE parse error: {}", e.what());
                }
            }
        };

        RetryConfig retry;
        int delay = retry.base_delay_ms;
        CURLcode res = CURLE_OK;
        long http_code = 0;

        for (int attempt = 0; attempt <= retry.max_retries; attempt++) {
            response_text.clear();
            input_tokens = 0;
            output_tokens = 0;
            has_usage = false;
            accumulated_tool_calls_json = nlohmann::json::array();
            line_buffer.clear();

            CURL* curl = curl_easy_init();
            if (!curl) {
                res = CURLE_OUT_OF_MEMORY;
                http_code = 0;
                if (attempt == retry.max_retries) {
                    throw AgentError(ErrorType::LLM_ERROR, "Failed to initialize curl handle");
                }
                spdlog::warn("Provider: retry {}/{} after {}ms (curl_easy_init failed)",
                    attempt + 1, retry.max_retries, delay);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                delay *= 2;
                continue;
            }
            struct curl_slist* hdrs = nullptr;
            hdrs = curl_slist_append(hdrs,
                ("Authorization: Bearer " + config_.api_key).c_str());
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body_str.c_str());
            hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
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

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                    auto* cb = static_cast<decltype(&write_callback)>(userdata);
                    std::string data(ptr, size * nmemb);
                    (*cb)(data);
                    return size * nmemb;
                });
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_callback);

            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_cleanup(curl);
            curl_slist_free_all(hdrs);

            // Success
            if (res == CURLE_OK && http_code < 400) break;

            // Never retry cancellation
            if (cancellation && cancellation->cancelled()) {
                throw AgentError(ErrorType::LLM_TIMEOUT, "LLM request cancelled");
            }

            // Never retry auth errors
            if (http_code == 401 || http_code == 403) {
                throw AgentError(ErrorType::LLM_ERROR,
                    "LLM authentication failed (HTTP " + std::to_string(http_code) + ")");
            }

            // Not retryable: other 4xx
            if (http_code >= 400 && http_code < 500 && http_code != 429) {
                throw AgentError(ErrorType::LLM_ERROR,
                    "LLM API error (HTTP " + std::to_string(http_code) + ")");
            }

            // Exhausted retries
            if (attempt == retry.max_retries) {
                if (res != CURLE_OK) {
                    throw AgentError(ErrorType::LLM_ERROR, curl_easy_strerror(res));
                }
                throw AgentError(ErrorType::LLM_ERROR,
                    "LLM API error after retries (HTTP " + std::to_string(http_code) + ")");
            }

            // Backoff and retry
            spdlog::warn("Provider: retry {}/{} after {}ms (HTTP {}, curl {})",
                attempt + 1, retry.max_retries, delay, http_code, (int)res);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            delay *= 2;
        }

        AgentResponse response;
        for (auto& tc_json : accumulated_tool_calls_json) {
            ToolCall tc;
            tc.id = tc_json.value("id", "");
            tc.name = tc_json["function"].value("name", "");
            tc.arguments = tc_json["function"].value("arguments", "");
            response.tool_calls.push_back(std::move(tc));
        }
        response.text = response_text;
        response.total_input_tokens = input_tokens;
        response.total_output_tokens = output_tokens;
        response.has_usage = has_usage;

        stats_.total_requests++;
        return response;
    });
}

nlohmann::json OpenAIProvider::build_messages(const std::vector<Message>& msgs) const {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& m : msgs) {
        nlohmann::json item;
        item["role"] = m.role;
        item["content"] = m.content;

        if (!m.tool_calls.empty()) {
            auto tcs = nlohmann::json::array();
            for (auto& tc : m.tool_calls) {
                nlohmann::json t;
                t["id"] = tc.id;
                t["type"] = "function";
                t["function"]["name"] = tc.name;
                t["function"]["arguments"] = tc.arguments;
                tcs.push_back(t);
            }
            item["tool_calls"] = tcs;
        }

        if (m.tool_call_id.has_value()) {
            item["tool_call_id"] = m.tool_call_id.value();
        }

        arr.push_back(item);
    }
    return arr;
}

nlohmann::json OpenAIProvider::build_tools(const std::vector<ToolSpec>& tools) const {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& t : tools) {
        nlohmann::json item;
        item["type"] = "function";
        item["function"]["name"] = t.name;
        item["function"]["description"] = t.description;
        if (!t.parameters_json.empty()) {
            item["function"]["parameters"] = nlohmann::json::parse(t.parameters_json);
        }
        arr.push_back(item);
    }
    return arr;
}

bool OpenAIProvider::test_connection() {
    std::string url = config_.api_base_url + "/chat/completions";
    std::string body_str = R"({"model":")" + config_.default_model +
        R"(","max_tokens":1,"messages":[{"role":"user","content":"Hi"}]})";
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + config_.api_key).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    long http_code = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK && http_code >= 200 && http_code < 300;
}

} // namespace merak
