#include <merak/lsp_tool.hpp>
#include <merak/json_rpc_client.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace merak::tools {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string detect_language(const std::string& file_path) {
    auto dot = file_path.rfind('.');
    if (dot == std::string::npos) return "";

    std::string ext = file_path.substr(dot);

    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" ||
        ext == ".cxx" || ext == ".c") {
        return "cpp";
    }
    if (ext == ".rs") return "rust";
    if (ext == ".py") return "python";
    if (ext == ".ts" || ext == ".tsx") return "typescript";
    if (ext == ".js" || ext == ".jsx") return "typescript";
    if (ext == ".go") return "go";
    return "";
}

static std::string detect_lsp_command(const std::string& lang) {
    static const std::map<std::string, std::string> lang_to_server = {
        {"cpp",        "clangd"},
        {"rust",       "rust-analyzer"},
        {"python",     "pyright"},
        {"typescript", "typescript-language-server"},
        {"go",         "gopls"},
    };

    auto it = lang_to_server.find(lang);
    if (it != lang_to_server.end()) return it->second;

    return "";
}

static std::string find_project_root(const std::string& file_path, const std::string& lang) {
    // Walk up from the file's directory looking for known project markers
    static const std::map<std::string, std::vector<std::string>> markers = {
        {"cpp",        {"compile_commands.json", "CMakeLists.txt", ".clangd"}},
        {"rust",       {"Cargo.toml", "Cargo.lock"}},
        {"python",     {"pyproject.toml", "setup.py", "setup.cfg", "Pipfile"}},
        {"typescript", {"package.json", "tsconfig.json", "jsconfig.json"}},
        {"go",         {"go.mod", "go.sum"}},
    };

    fs::path dir = fs::absolute(fs::path(file_path)).parent_path();

    auto it = markers.find(lang);
    if (it == markers.end()) return dir.string();

    while (!dir.empty() && dir.has_parent_path()) {
        for (const auto& marker : it->second) {
            if (fs::exists(dir / marker)) {
                return dir.string();
            }
        }
        dir = dir.parent_path();
    }

    return fs::path(file_path).parent_path().string();
}

static std::string read_file_content(const std::string& file_path) {
    std::ifstream f(file_path, std::ios::in | std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string file_uri(const std::string& file_path) {
    return "file://" + file_path;
}

// LSP uses 0-based lines and UTF-16 character offsets.
// Our tool receives 1-based line/column, so convert.
static int to_lsp_line(int one_based_line) {
    return std::max(0, one_based_line - 1);
}
static int to_lsp_column(int one_based_column) {
    return std::max(0, one_based_column - 1);
}

// ---------------------------------------------------------------------------
// ToolSpec
// ---------------------------------------------------------------------------

ToolSpec LspTool::spec() const {
    ToolSpec s;
    s.name = "lsp";
    s.description =
        "Language Server Protocol tool for deep code analysis. "
        "Operations: go_to_definition, find_references, hover, diagnostics, "
        "document_symbols, format";
    s.source = "builtin";
    s.category = Category::Consultative;
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "description": "LSP operation to perform",
                "enum": ["go_to_definition", "find_references", "hover",
                         "diagnostics", "document_symbols", "format"]
            },
            "file_path": {
                "type": "string",
                "description": "Absolute path to the source file"
            },
            "line": {
                "type": "integer",
                "description": "1-based line number for cursor-dependent operations"
            },
            "column": {
                "type": "integer",
                "description": "1-based column number for cursor-dependent operations"
            }
        },
        "required": ["action", "file_path"]
    })JSON";
    return s;
}

ToolMeta LspTool::meta() const {
    ToolMeta m;
    m.name = "lsp";
    m.description = "Language Server Protocol: go_to_definition, find_references, hover, rename, code_action, diagnostics, formatting";
    m.triggers = {"lsp", "language server", "definition", "references", "hover", "rename", "diagnostics"};
    m.pinned = false;
    m.intents = {IntentType::CodeIntel};
    m.scope = Scope::Local;
    m.schema_tokens = 90;
    return m;
}

// ---------------------------------------------------------------------------
// Permission
// ---------------------------------------------------------------------------

PermissionLevel LspTool::permission() const {
    return PermissionLevel::safe;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

std::future<ToolResult> LspTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::async, [call = std::move(call), session_mutex = session_mutex_]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);

            std::string action    = args.value("action", "");
            std::string file_path = args.value("file_path", "");
            int line   = args.value("line", 0);
            int column = args.value("column", 0);

            // --- Validation ---
            if (file_path.empty()) {
                result.is_error = true;
                result.output = nlohmann::json{
                    {"status", "error"},
                    {"message", "file_path is required"}
                }.dump();
                return result;
            }

            if (!fs::exists(file_path)) {
                result.is_error = true;
                result.output = nlohmann::json{
                    {"status", "error"},
                    {"message", "file not found"},
                    {"file", file_path}
                }.dump();
                return result;
            }

            static const std::vector<std::string> valid_actions = {
                "go_to_definition", "find_references", "hover",
                "diagnostics", "document_symbols", "format"
            };

            bool action_valid = false;
            for (const auto& a : valid_actions) {
                if (action == a) { action_valid = true; break; }
            }
            if (!action_valid) {
                result.is_error = true;
                result.output = nlohmann::json{
                    {"status", "error"},
                    {"message", "unknown action: " + action},
                    {"valid_actions", valid_actions}
                }.dump();
                return result;
            }

            // --- Language / LSP server detection ---
            std::string lang = detect_language(file_path);
            if (lang.empty()) {
                result.is_error = true;
                result.output = nlohmann::json{
                    {"status", "error"},
                    {"message", "cannot detect language for file"},
                    {"file", file_path}
                }.dump();
                return result;
            }

            std::string server_cmd = detect_lsp_command(lang);
            if (server_cmd.empty()) {
                result.is_error = true;
                result.output = nlohmann::json{
                    {"status", "error"},
                    {"message", "no LSP server known for language: " + lang},
                    {"language", lang}
                }.dump();
                return result;
            }

            // --- Project root ---
            std::string root_path = find_project_root(file_path, lang);
            std::string root_uri = file_uri(root_path);

            // --- Read file content for didOpen ---
            std::string file_content = read_file_content(file_path);
            if (file_content.empty() && action != "diagnostics") {
                result.is_error = true;
                result.output = nlohmann::json{
                    {"status", "error"},
                    {"message", "cannot read file content"},
                    {"file", file_path}
                }.dump();
                return result;
            }

            // --- Start LSP server ---
            // Serialize LSP sessions with a mutex (one LSP process at a time)
            std::unique_lock<std::mutex> lock;
            if (session_mutex) {
                lock = std::unique_lock<std::mutex>(*session_mutex);
            }

            JsonRpcClient client;
            std::vector<std::string> server_args = {"--log=error"};
            if (!client.start(server_cmd, server_args)) {
                result.is_error = true;
                result.output = nlohmann::json{
                    {"status", "error"},
                    {"message", "failed to start LSP server: " + server_cmd},
                    {"language", lang}
                }.dump();
                return result;
            }

            // --- Send initialize request ---
            {
                nlohmann::json init_params;
                init_params["processId"] = nullptr;
                init_params["rootUri"] = root_uri;
                init_params["capabilities"]["textDocument"]["definition"]["linkSupport"] = true;
                init_params["capabilities"]["textDocument"]["references"]["dynamicRegistration"] = true;
                init_params["capabilities"]["textDocument"]["hover"]["contentFormat"] = {"markdown", "plaintext"};
                init_params["capabilities"]["textDocument"]["documentSymbol"]["hierarchicalDocumentSymbolSupport"] = true;
                init_params["capabilities"]["textDocument"]["formatting"]["dynamicRegistration"] = true;
                init_params["capabilities"]["textDocument"]["diagnostic"]["dynamicRegistration"] = true;

                auto init_resp = client.send_request("initialize", init_params).get();
                if (init_resp.contains("error")) {
                    client.stop();
                    result.is_error = true;
                    result.output = nlohmann::json{
                        {"status", "error"},
                        {"message", "LSP initialize failed: " + init_resp["error"].value("message", "unknown error")},
                        {"server", server_cmd}
                    }.dump();
                    return result;
                }
                spdlog::debug("LspTool: {} initialized for {}", server_cmd, root_uri);
            }

            // --- Send initialized notification ---
            client.send_notification("initialized", nlohmann::json::object());

            // --- Send textDocument/didOpen ---
            {
                nlohmann::json did_open_params;
                did_open_params["textDocument"]["uri"] = file_uri(file_path);
                did_open_params["textDocument"]["languageId"] = lang;
                did_open_params["textDocument"]["version"] = 1;
                did_open_params["textDocument"]["text"] = file_content;
                client.send_notification("textDocument/didOpen", did_open_params);
            }

            // --- Build and send the specific request ---
            std::string lsp_method;
            nlohmann::json req_params;

            if (action == "go_to_definition") {
                lsp_method = "textDocument/definition";
                req_params["textDocument"]["uri"] = file_uri(file_path);
                req_params["position"]["line"] = to_lsp_line(line);
                req_params["position"]["character"] = to_lsp_column(column);
            } else if (action == "find_references") {
                lsp_method = "textDocument/references";
                req_params["textDocument"]["uri"] = file_uri(file_path);
                req_params["position"]["line"] = to_lsp_line(line);
                req_params["position"]["character"] = to_lsp_column(column);
                req_params["context"]["includeDeclaration"] = true;
            } else if (action == "hover") {
                lsp_method = "textDocument/hover";
                req_params["textDocument"]["uri"] = file_uri(file_path);
                req_params["position"]["line"] = to_lsp_line(line);
                req_params["position"]["character"] = to_lsp_column(column);
            } else if (action == "diagnostics") {
                lsp_method = "textDocument/diagnostic";
                req_params["textDocument"]["uri"] = file_uri(file_path);
            } else if (action == "document_symbols") {
                lsp_method = "textDocument/documentSymbol";
                req_params["textDocument"]["uri"] = file_uri(file_path);
            } else if (action == "format") {
                lsp_method = "textDocument/formatting";
                req_params["textDocument"]["uri"] = file_uri(file_path);
                req_params["options"]["tabSize"] = 4;
                req_params["options"]["insertSpaces"] = true;
            }

            spdlog::debug("LspTool: sending {} for {}", lsp_method, file_path);
            auto lsp_resp = client.send_request(lsp_method, req_params).get();

            // --- Clean up ---
            // Send didClose before stopping
            {
                nlohmann::json did_close_params;
                did_close_params["textDocument"]["uri"] = file_uri(file_path);
                client.send_notification("textDocument/didClose", did_close_params);
            }
            client.send_notification("shutdown", nlohmann::json::object());
            client.stop();

            // --- Parse response into structured output ---
            nlohmann::json out;
            out["status"]    = "ok";
            out["operation"] = action;
            out["file"]      = file_path;
            out["language"]  = lang;
            out["server"]    = server_cmd;
            out["lsp_method"] = lsp_method;

            if (lsp_resp.contains("error")) {
                out["status"] = "error";
                out["message"] = lsp_resp["error"].value("message", "LSP request failed");
                result.is_error = true;
            } else if (lsp_resp.contains("result")) {
                auto& lsp_result = lsp_resp["result"];
                if (action == "go_to_definition") {
                    if (lsp_result.is_array() && !lsp_result.empty()) {
                        out["definitions"] = lsp_result;
                    } else if (lsp_result.is_object()) {
                        out["definition"] = lsp_result;
                    } else if (lsp_result.is_null()) {
                        out["definitions"] = nlohmann::json::array();
                        out["message"] = "No definition found";
                    }
                } else if (action == "find_references") {
                    out["references"] = lsp_result.is_array() ? lsp_result : nlohmann::json::array();
                    out["count"] = out["references"].size();
                } else if (action == "hover") {
                    if (lsp_result.contains("contents")) {
                        auto& contents = lsp_result["contents"];
                        if (contents.is_string()) {
                            out["hover_text"] = contents.get<std::string>();
                        } else if (contents.is_object() && contents.contains("value")) {
                            out["hover_text"] = contents["value"].get<std::string>();
                        } else {
                            out["hover"] = contents;
                        }
                    } else {
                        out["hover"] = lsp_result;
                    }
                } else if (action == "diagnostics") {
                    out["diagnostics"] = lsp_result;
                    if (lsp_result.contains("items")) {
                        out["count"] = lsp_result["items"].size();
                    }
                } else if (action == "document_symbols") {
                    out["symbols"] = lsp_result;
                    if (lsp_result.is_array()) {
                        out["count"] = lsp_result.size();
                    }
                } else if (action == "format") {
                    if (lsp_result.is_array() && !lsp_result.empty()) {
                        out["edits"] = lsp_result;
                        out["count"] = lsp_result.size();
                    } else {
                        out["edits"] = nlohmann::json::array();
                        out["message"] = "No formatting changes needed";
                    }
                }
            } else {
                out["raw_response"] = lsp_resp;
            }

            result.output = out.dump();

        } catch (const nlohmann::json::parse_error& e) {
            result.is_error = true;
            result.output = nlohmann::json{
                {"status", "error"},
                {"message", std::string("LSP tool JSON parse error: ") + e.what()}
            }.dump();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = nlohmann::json{
                {"status", "error"},
                {"message", std::string("LSP tool error: ") + e.what()}
            }.dump();
        }

        return result;
    });
}

// ---------------------------------------------------------------------------
// Clone
// ---------------------------------------------------------------------------

std::unique_ptr<Tool> LspTool::clone() const {
    return std::make_unique<LspTool>(session_mutex_);
}

} // namespace merak::tools
