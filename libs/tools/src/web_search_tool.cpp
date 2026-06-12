#include <merak/web_search_tool.hpp>

#include <nlohmann/json.hpp>

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
            auto engine = args.value("engine", "duckduckgo");
            auto encoded = url_encode_query(query);

            std::string url;
            std::string engine_name;
            if (engine == "google") {
                url = "https://www.google.com/search?q=" + encoded;
                engine_name = "Google";
            } else if (engine == "bing") {
                url = "https://www.bing.com/search?q=" + encoded;
                engine_name = "Bing";
            } else {
                url = "https://duckduckgo.com/?q=" + encoded;
                engine_name = "DuckDuckGo";
            }

            std::ostringstream out;
            out << "## " << engine_name << " Search\n\n"
                << "**Query:** " << query << "\n\n"
                << "Click to search: [" << query << "](" << url << ")\n\n"
                << "> No live HTTP request performed. Open the link in your browser to see results.\n";

            result.output = out.str();
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> WebSearchTool::clone() const {
    return std::make_unique<WebSearchTool>();
}

} // namespace merak::tools
