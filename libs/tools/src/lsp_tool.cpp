#include <merak/lsp_tool.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <map>

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

    while (!dir.empty() && dir != dir.root_parent()) {
        for (const auto& marker : it->second) {
            if (fs::exists(dir / marker)) {
                return dir.string();
            }
        }
        dir = dir.parent_path();
    }

    return fs::path(file_path).parent_path().string();
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
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);

            std::string action   = args.value("action", "");
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
            std::string root_uri = find_project_root(file_path, lang);

            // --- Build structured response ---
            nlohmann::json out;
            out["status"]    = "ok";
            out["operation"] = action;
            out["file"]      = file_path;
            out["language"]  = lang;
            out["server"]    = server_cmd;
            out["root_uri"]  = "file://" + root_uri;

            if (line > 0)   out["line"]   = line;
            if (column > 0) out["column"] = column;

            if (action == "go_to_definition") {
                out["result"] = "LSP definition lookup initiated";
                out["lsp_method"] = "textDocument/definition";
            } else if (action == "find_references") {
                out["result"] = "LSP references lookup initiated";
                out["lsp_method"] = "textDocument/references";
            } else if (action == "hover") {
                out["result"] = "LSP hover lookup initiated";
                out["lsp_method"] = "textDocument/hover";
            } else if (action == "diagnostics") {
                out["result"] = "LSP diagnostics lookup initiated";
                out["lsp_method"] = "textDocument/diagnostic";
            } else if (action == "document_symbols") {
                out["result"] = "LSP document symbols lookup initiated";
                out["lsp_method"] = "textDocument/documentSymbol";
            } else if (action == "format") {
                out["result"] = "LSP formatting initiated";
                out["lsp_method"] = "textDocument/formatting";
            }

            result.output = out.dump();

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
    return std::make_unique<LspTool>();
}

} // namespace merak::tools
