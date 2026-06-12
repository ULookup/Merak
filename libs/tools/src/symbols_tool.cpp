#include <merak/symbols_tool.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace merak::tools {

static constexpr size_t kMaxFileSize = 1 * 1024 * 1024; // 1 MB

static std::string auto_detect_language(const std::string& file_path) {
    std::string ext;
    auto dot = file_path.rfind('.');
    if (dot != std::string::npos) {
        ext = file_path.substr(dot);
    }

    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
        return "c_cpp";
    } else if (ext == ".rs") {
        return "rust";
    } else if (ext == ".py") {
        return "python";
    } else if (ext == ".ts" || ext == ".js" || ext == ".jsx" || ext == ".tsx") {
        return "typescript";
    } else if (ext == ".go") {
        return "go";
    }
    return "";
}

struct SymbolGroup {
    std::string name;
    std::vector<std::string> symbols;
};

struct SymbolResult {
    std::vector<SymbolGroup> groups;
    std::vector<std::string> imports; // for Python/TS imports
};

static SymbolResult extract_c_cpp_symbols(const std::string& content) {
    SymbolResult sr;
    SymbolGroup classes, structs, enums, namespaces, templates_group, functions;

    classes.name = "Classes";
    structs.name = "Structs";
    enums.name = "Enums";
    namespaces.name = "Namespaces";
    templates_group.name = "Templates";
    functions.name = "Functions";

    // class/struct/enum/namespace/template declarations
    std::regex decl_re(R"(^\s*(class|struct|enum|namespace|template)\s+(\w+))", std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), decl_re);
         it != std::sregex_iterator(); ++it) {
        std::string kind = (*it)[1];
        std::string name = (*it)[2];
        if (kind == "class") classes.symbols.push_back(name);
        else if (kind == "struct") structs.symbols.push_back(name);
        else if (kind == "enum") enums.symbols.push_back(name);
        else if (kind == "namespace") namespaces.symbols.push_back(name);
        else if (kind == "template") templates_group.symbols.push_back(name);
    }

    // Function declarations (simplified: return_type func_name( ... )
    std::regex func_re(R"(^\s*(?:[\w:]+\s+)+?(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:\{|;))", std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), func_re);
         it != std::sregex_iterator(); ++it) {
        std::string name = (*it)[1];
        // Skip keywords
        if (name == "if" || name == "while" || name == "for" || name == "switch" ||
            name == "return" || name == "catch") continue;
        functions.symbols.push_back(name);
    }

    for (auto* g : {&classes, &structs, &enums, &namespaces, &templates_group, &functions}) {
        if (!g->symbols.empty()) sr.groups.push_back(*g);
    }
    return sr;
}

static SymbolResult extract_rust_symbols(const std::string& content) {
    SymbolResult sr;
    SymbolGroup functions, structs, enums, traits, impls;

    functions.name = "Functions";
    structs.name = "Structs";
    enums.name = "Enums";
    traits.name = "Traits";
    impls.name = "Impls";

    std::regex decl_re(R"(^\s*(pub\s+)?(fn|struct|enum|trait|impl)\s+(\w+))", std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), decl_re);
         it != std::sregex_iterator(); ++it) {
        std::string kind = (*it)[2];
        std::string name = (*it)[3];
        if (kind == "fn") functions.symbols.push_back(name);
        else if (kind == "struct") structs.symbols.push_back(name);
        else if (kind == "enum") enums.symbols.push_back(name);
        else if (kind == "trait") traits.symbols.push_back(name);
        else if (kind == "impl") impls.symbols.push_back(name);
    }

    for (auto* g : {&functions, &structs, &enums, &traits, &impls}) {
        if (!g->symbols.empty()) sr.groups.push_back(*g);
    }
    return sr;
}

static SymbolResult extract_python_symbols(const std::string& content) {
    SymbolResult sr;
    SymbolGroup classes, functions;

    classes.name = "Classes";
    functions.name = "Functions";

    std::regex class_re(R"(^\s*class\s+(\w+))", std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), class_re);
         it != std::sregex_iterator(); ++it) {
        classes.symbols.push_back((*it)[1]);
    }

    std::regex func_re(R"(^\s*def\s+(\w+))", std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), func_re);
         it != std::sregex_iterator(); ++it) {
        functions.symbols.push_back((*it)[1]);
    }

    // Imports
    std::regex import_re(R"(^\s*(?:from\s+(\S+)\s+)?import\s+(.+))", std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), import_re);
         it != std::sregex_iterator(); ++it) {
        sr.imports.push_back((*it)[0]);
    }

    if (!classes.symbols.empty()) sr.groups.push_back(classes);
    if (!functions.symbols.empty()) sr.groups.push_back(functions);
    return sr;
}

static SymbolResult extract_typescript_symbols(const std::string& content) {
    SymbolResult sr;
    SymbolGroup functions, classes, exports_group, consts;

    functions.name = "Functions";
    classes.name = "Classes";
    exports_group.name = "Exports";
    consts.name = "Constants";

    std::regex decl_re(R"(^\s*(function|class|export\s+(?:const|function|class|default)?|const)\s+(\w+))",
                       std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), decl_re);
         it != std::sregex_iterator(); ++it) {
        std::string kind = (*it)[1];
        std::string name = (*it)[2];
        if (kind == "function") functions.symbols.push_back(name);
        else if (kind == "class") classes.symbols.push_back(name);
        else if (kind.find("export") != std::string::npos) exports_group.symbols.push_back(name);
        else if (kind == "const") consts.symbols.push_back(name);
    }

    for (auto* g : {&functions, &classes, &exports_group, &consts}) {
        if (!g->symbols.empty()) sr.groups.push_back(*g);
    }
    return sr;
}

static SymbolResult extract_go_symbols(const std::string& content) {
    SymbolResult sr;
    SymbolGroup functions, types, vars, consts;

    functions.name = "Functions";
    types.name = "Types";
    vars.name = "Variables";
    consts.name = "Constants";

    std::regex decl_re(R"(^\s*(func|type|var|const)\s+(\w+))", std::regex::multiline);
    for (auto it = std::sregex_iterator(content.begin(), content.end(), decl_re);
         it != std::sregex_iterator(); ++it) {
        std::string kind = (*it)[1];
        std::string name = (*it)[2];
        if (kind == "func") functions.symbols.push_back(name);
        else if (kind == "type") types.symbols.push_back(name);
        else if (kind == "var") vars.symbols.push_back(name);
        else if (kind == "const") consts.symbols.push_back(name);
    }

    for (auto* g : {&functions, &types, &vars, &consts}) {
        if (!g->symbols.empty()) sr.groups.push_back(*g);
    }
    return sr;
}

static std::string format_symbols_markdown(const SymbolResult& sr) {
    std::ostringstream out;
    for (const auto& group : sr.groups) {
        out << "## " << group.name << " (" << group.symbols.size() << ")\n\n";
        for (const auto& sym : group.symbols) {
            out << "- `" << sym << "`\n";
        }
        out << "\n";
    }
    if (!sr.imports.empty()) {
        out << "## Imports (" << sr.imports.size() << ")\n\n";
        for (const auto& imp : sr.imports) {
            out << "- `" << imp << "`\n";
        }
        out << "\n";
    }
    return out.str();
}

// --- Main Tool ---

ToolSpec SymbolsTool::spec() const {
    ToolSpec s;
    s.name = "symbols";
    s.description = "Extract function/class/struct signatures from a file using regex analysis";
    s.source = "builtin";
    s.category = Category::ReadOnly;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "file_path": {
                "type": "string",
                "description": "Path to the source file to analyze"
            },
            "language": {
                "type": "string",
                "description": "Source language (auto-detected from extension if not specified)"
            }
        },
        "required": ["file_path"]
    })";
    return s;
}

PermissionLevel SymbolsTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> SymbolsTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto file_path = args.value("file_path", "");
            auto language = args.value("language", "");

            if (file_path.empty()) {
                result.output = "file_path is required";
                result.is_error = true;
                return result;
            }

            // Check file exists and size
            std::error_code ec;
            auto file_size = std::filesystem::file_size(file_path, ec);
            if (ec) {
                result.output = "Cannot read file: " + ec.message();
                result.is_error = true;
                return result;
            }

            if (static_cast<size_t>(file_size) > kMaxFileSize) {
                result.output = "File too large: " + std::to_string(file_size) +
                                " bytes (max " + std::to_string(kMaxFileSize) + ")";
                result.is_error = true;
                return result;
            }

            // Read file
            std::ifstream file(file_path);
            if (!file.is_open()) {
                result.output = "Failed to open file: " + file_path;
                result.is_error = true;
                return result;
            }
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();

            // Auto-detect language if not specified
            if (language.empty()) {
                language = auto_detect_language(file_path);
            }

            // Extract symbols
            SymbolResult sr;
            if (language == "c_cpp") {
                sr = extract_c_cpp_symbols(content);
            } else if (language == "rust") {
                sr = extract_rust_symbols(content);
            } else if (language == "python") {
                sr = extract_python_symbols(content);
            } else if (language == "typescript") {
                sr = extract_typescript_symbols(content);
            } else if (language == "go") {
                sr = extract_go_symbols(content);
            } else {
                result.output = "Unsupported or unknown language: " + language +
                                ". Supported: c_cpp, rust, python, typescript, go";
                result.is_error = true;
                return result;
            }

            auto markdown = format_symbols_markdown(sr);
            if (markdown.empty()) {
                markdown = "No symbols found in " + file_path + "\n";
            }
            result.output = markdown;
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> SymbolsTool::clone() const {
    return std::make_unique<SymbolsTool>();
}

} // namespace merak::tools
