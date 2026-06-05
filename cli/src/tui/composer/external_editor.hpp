#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/wait.h>

namespace merak::tui {

struct ExternalEditorResolver {
    std::string visual;
    std::string editor;
    std::string git_core_editor;

    static std::string shell_output(const char* command) {
        std::array<char, 256> buffer{};
        std::string output;
        FILE* pipe = popen(command, "r");
        if (!pipe) return "";
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
        pclose(pipe);
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
        return output;
    }

    static bool command_exists(const std::string& command) {
        auto first = command.substr(0, command.find(' '));
        auto result = shell_output(("command -v " + first + " 2>/dev/null").c_str());
        return !result.empty();
    }

    static ExternalEditorResolver from_environment() {
        ExternalEditorResolver resolver;
        if (auto* value = std::getenv("VISUAL")) resolver.visual = value;
        if (auto* value = std::getenv("EDITOR")) resolver.editor = value;
        resolver.git_core_editor = shell_output("git config --get core.editor 2>/dev/null");
        return resolver;
    }

    std::string resolve() const {
        if (!visual.empty()) return visual;
        if (!editor.empty()) return editor;
        if (!git_core_editor.empty()) return git_core_editor;
        for (const auto& candidate : {"nvim", "vim", "vi", "nano"}) {
            if (command_exists(candidate)) return candidate;
        }
        return "";
    }
};

inline std::string shell_quote(const std::filesystem::path& path) {
    std::string text = path.string();
    std::string out = "'";
    for (char c : text) out += c == '\'' ? "'\\''" : std::string(1, c);
    out += "'";
    return out;
}

inline std::optional<std::string> edit_text_external(std::string initial) {
    auto editor = ExternalEditorResolver::from_environment().resolve();
    if (editor.empty()) return std::nullopt;
    auto file = std::filesystem::temp_directory_path()
        / ("merak-editor-" + std::to_string(std::rand()) + ".md");
    {
        std::ofstream output(file);
        output << initial;
    }
    auto command = editor + " " + shell_quote(file);
    auto status = std::system(command.c_str());
    if (status == -1 || (WIFEXITED(status) && WEXITSTATUS(status) != 0)) {
        std::filesystem::remove(file);
        return std::nullopt;
    }
    std::ifstream input(file);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::filesystem::remove(file);
    return buffer.str();
}

} // namespace merak::tui
