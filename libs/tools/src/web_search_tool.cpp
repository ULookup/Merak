#include <merak/web_search_tool.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <curl/curl.h>
#include <future>
#include <memory>
#include <sstream>
#include <string>

namespace merak::tools {

static std::string url_encode_query(const std::string& query) {
    std::string encoded;
    encoded.reserve(query.size() * 2);
    for (char c : query) {
        if (c == ' ') {
            encoded += '+';
        } else if (std::isalnum(static_cast<unsigned char>(c)) ||
                   c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            encoded += buf;
        }
    }
    return encoded;
}

namespace {

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append(static_cast<char*>(contents), total);
    return total;
}

std::string http_get(const std::string& url) {
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Merak/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "";
    return response;
}

} // anonymous namespace

ToolSpec WebSearchTool::spec() const {
    ToolSpec s;
    s.name = "web_search";
    s.description = "Search the web across multiple engines";
    s.source = "builtin";
    s.category = Category::Consultative;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "The search query string"
            },
            "engine": {
                "type": "string",
                "enum": ["google", "bing", "duckduckgo"],
                "description": "Search engine to use",
                "default": "duckduckgo"
            },
            "max_results": {
                "type": "integer",
                "description": "Maximum number of results",
                "default": 10
            }
        },
        "required": ["query"]
    })";
    return s;
}

ToolMeta WebSearchTool::meta() const {
    ToolMeta m;
    m.name = "web_search";
    m.description = "Search the web across multiple engines";
    m.triggers = {"web search", "search web", "search online", "google"};
    m.pinned = false;
    m.intents = {IntentType::Network};
    m.scope = Scope::External;
    m.schema_tokens = 25;
    return m;
}

PermissionLevel WebSearchTool::permission() const {
    return PermissionLevel::ask;
}

std::future<ToolResult> WebSearchTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto query = args.value("query", "");
            auto max_results = args.value("max_results", 10);

            if (query.empty()) {
                result.output = R"({"error":"query is required"})";
                result.is_error = true;
                return result;
            }

            auto encoded = url_encode_query(query);
            std::string url = "https://html.duckduckgo.com/html/?q=" + encoded;
            auto html = http_get(url);

            if (html.empty()) {
                result.output = R"({"error":"Search request failed. Check network connectivity."})";
                result.is_error = true;
                return result;
            }

            nlohmann::json results = nlohmann::json::array();
            std::string::size_type pos = 0;
            int count = 0;

            while (count < max_results) {
                auto snippet_start = html.find("class=\"result__snippet\"", pos);
                if (snippet_start == std::string::npos) break;

                auto content_start = html.find('>', snippet_start) + 1;
                auto content_end = html.find("</", content_start);
                if (content_start == std::string::npos || content_end == std::string::npos) break;

                std::string snippet = html.substr(content_start, content_end - content_start);

                auto link_start = html.rfind("class=\"result__url\"", snippet_start);
                std::string link;
                if (link_start != std::string::npos && link_start > pos) {
                    auto href_start = html.find("href=\"", link_start);
                    if (href_start != std::string::npos && href_start < snippet_start) {
                        href_start += 6;
                        auto href_end = html.find('"', href_start);
                        if (href_end != std::string::npos) {
                            link = html.substr(href_start, href_end - href_start);
                        }
                    }
                }

                auto title_start = html.rfind("class=\"result__title\"", snippet_start);
                std::string title;
                if (title_start != std::string::npos && title_start > pos) {
                    auto title_content_start = html.find('>', html.find('>', title_start) + 1) + 1;
                    auto title_content_end = html.find("</", title_content_start);
                    if (title_content_start != std::string::npos && title_content_end != std::string::npos) {
                        title = html.substr(title_content_start, title_content_end - title_content_start);
                        while (true) {
                            auto tag = title.find('<');
                            if (tag == std::string::npos) break;
                            auto tag_end = title.find('>', tag);
                            if (tag_end == std::string::npos) break;
                            title.erase(tag, tag_end - tag + 1);
                        }
                    }
                }

                nlohmann::json item;
                item["title"] = title;
                item["snippet"] = snippet;
                item["url"] = link;
                results.push_back(item);

                pos = content_end;
                count++;
            }

            nlohmann::json out;
            out["query"] = query;
            out["results"] = results;
            out["count"] = results.size();
            out["engine"] = "duckduckgo";
            result.output = out.dump();

        } catch (const std::exception& e) {
            result.output = nlohmann::json{{"error", std::string("Search error: ") + e.what()}}.dump();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> WebSearchTool::clone() const {
    return std::make_unique<WebSearchTool>();
}

} // namespace merak::tools
