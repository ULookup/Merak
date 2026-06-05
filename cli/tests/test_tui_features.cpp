#include "tui/chat_timeline.hpp"
#include "tui/components/status_bar.hpp"
#include "tui/composer/chat_composer.hpp"
#include "tui/composer/external_editor.hpp"
#include "tui/composer/mention_menu.hpp"
#include "tui/history_cell/welcome_cell.hpp"
#include "theme/theme.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

using namespace merak::tui;

static bool contains(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

int main() {
    {
        AssistantCell cell;
        cell.append("```cpp\nint main() { return 0; }\n```\n\n| Name | Count |\n| --- | ---: |\n| tea | 3 |\n+ added\n- removed\n<think>\nprivate chain\n</think>\n");
        cell.finalize();
        Buffer buf;
        buf.resize(100, 80);
        cell.render(buf, 100);
        auto rendered = buffer_to_lines(buf);
        assert(contains(rendered, "cpp"));
        assert(contains(rendered, "return"));
        assert(contains(rendered, "Name"));
        assert(contains(rendered, "Count"));
        assert(contains(rendered, "tea"));
        assert(contains(rendered, "added"));
        assert(contains(rendered, "removed"));
        assert(contains(rendered, "thinking hidden"));
        assert(!contains(rendered, "private chain"));
    }

    {
        // Test bold rendering
        AssistantCell bold_cell;
        bold_cell.append("**bold text**\n");
        bold_cell.finalize();
        Buffer buf;
        buf.resize(100, 80);
        bold_cell.render(buf, 100);
        auto rendered = buffer_to_lines(buf);
        bool found_bold = false;
        for (const auto& line : rendered) {
            if (line.find("bold text") != std::string::npos) {
                found_bold = true;
                break;
            }
        }
        assert(found_bold);
    }
    {
        // Test that list item with bold works
        AssistantCell list_bold;
        list_bold.append("* **bold item**\n");
        list_bold.finalize();
        Buffer buf;
        buf.resize(100, 80);
        list_bold.render(buf, 100);
        auto rendered = buffer_to_lines(buf);
        bool found_bold = false;
        for (const auto& line : rendered) {
            if (line.find("bold item") != std::string::npos) {
                found_bold = true;
                break;
            }
        }
        assert(found_bold);
    }

    {
        // CJK characters must survive sanitization
        auto sanitized = sanitize_terminal_text("你好世界");
        assert(sanitized == "你好世界");
        // Mixed ASCII + CJK
        auto mixed = sanitize_terminal_text("Hello 你好 World");
        assert(mixed == "Hello 你好 World");
        // ANSI escapes stripped, CJK preserved
        auto with_ansi = sanitize_terminal_text("\x1b[31m红色\x1b[0m");
        assert(with_ansi == "红色");
        // Invalid UTF-8 bytes stripped
        auto invalid = sanitize_terminal_text("abc\xff\xfe");
        assert(invalid == "abc");
    }

    {
        StatusBar bar;
        bar.set_provider("openai");
        bar.set_model("gpt-4o");
        bar.set_state("Thinking");
        bar.set_git_branch("main");
        bar.set_cwd("/Users/example/a/very/long/project/path");
        bar.set_permission_mode("Prompt");
        bar.set_pending_approvals(3);
        bar.set_running_agents(2);
        bar.set_token_budget(100, 80);
        bar.set_estimated_cost(0.42);
        auto wide = bar.plain_text(1, 160);
        assert(wide.find("main") != std::string::npos);
        assert(wide.find("80%") != std::string::npos);
        assert(wide.find("$0.42") != std::string::npos);
        assert(wide.find("pending 3") != std::string::npos);
        auto narrow = bar.plain_text(1, 38);
        assert(narrow.find("openai") != std::string::npos);
        assert(narrow.size() <= 38);
    }

    {
        ExternalEditorResolver resolver;
        resolver.visual = "code --wait";
        resolver.editor = "vim";
        assert(resolver.resolve() == "code --wait");
        resolver.visual.clear();
        assert(resolver.resolve() == "vim");
        resolver.editor.clear();
        resolver.git_core_editor = "nano";
        assert(resolver.resolve() == "nano");
    }

    {
        auto root = std::filesystem::temp_directory_path() / "merak_mention_test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "src");
        std::filesystem::create_directories(root / ".git");
        std::ofstream(root / "src" / "main.cpp") << "int main(){}";
        std::ofstream(root / ".git" / "config") << "ignored";
        FileProvider provider(root);
        auto files = provider.files();
        assert(files.size() == 1);
        assert(files[0] == "src/main.cpp");
        MentionMenu menu(provider);
        ChatComposer composer;
        composer.set_text("read @src/ma");
        assert(menu.update_from(composer.text(), composer.cursor()));
        assert(menu.matches().front().path == "src/main.cpp");
        composer.replace_range(menu.trigger_start(), composer.cursor(), menu.accepted_text());
        assert(composer.text() == "read @src/main.cpp ");
        std::filesystem::remove_all(root);
    }

    {
        ChatTimeline timeline;
        timeline.submit_user("hello");
        auto drained = timeline.drain_scrollback(80);
        assert(contains(drained, "hello"));
        assert(timeline.drain_scrollback(80).empty());
    }

    {
        ChatTimeline timeline;
        timeline.submit_user("pending");
        auto pending = timeline.pending_scrollback(80);
        assert(contains(pending, "pending"));
        assert(contains(timeline.pending_scrollback(80), "pending"));
        timeline.mark_scrollback_drained();
        assert(timeline.pending_scrollback(80).empty());
    }

    {
        ChatTimeline timeline;
        timeline.submit_user("existing content");
        timeline.append_assistant("new content");
        auto drained = timeline.drain_scrollback(80);
        assert(contains(drained, "existing content"));
        assert(!contains(drained, "new content"));

        Buffer active;
        active.resize(80, 8);
        auto active_rows = timeline.render_active(active, 80, 8);
        assert(active_rows > 0);
        assert(contains(buffer_to_lines(active), "new content"));

        timeline.commit_active();
        drained = timeline.drain_scrollback(80);
        assert(contains(drained, "new content"));
    }

    {
        WelcomeCell cell("0.1.0", "deepseek-v4-pro", "fix/tui");
        Buffer buf;
        buf.resize(80, 40);
        cell.render(buf, 80);
        auto rendered = buffer_to_lines(buf);
        // figlet title (6 lines)
        assert(rendered.size() == 10); // 6 figlet + 4 box
        assert(rendered[0].find("███╗") != std::string::npos);
        assert(rendered[5].find("╚╝") != std::string::npos);
        // info row
        assert(contains(rendered, "agent 0.1.0"));
        assert(contains(rendered, "model deepseek-v4-pro"));
        assert(contains(rendered, "branch fix/tui"));
        // tips row
        assert(contains(rendered, "/help"));
        assert(contains(rendered, "Ctrl+T"));
        assert(contains(rendered, "Ctrl+O"));
        // box borders
        assert(rendered[6].find("┌") != std::string::npos);
        assert(contains(rendered, "└"));
        // to_json
        auto json = cell.to_json();
        assert(json["type"] == "welcome");
    }

    {
        // narrow terminal
        WelcomeCell cell("0.1.0", "m", "b");
        Buffer buf;
        buf.resize(30, 40);
        cell.render(buf, 30);
        auto rendered = buffer_to_lines(buf);
        assert(rendered.size() == 10);
        assert(contains(rendered, "agent 0.1.0"));
        assert(contains(rendered, "model m"));
        assert(contains(rendered, "branch b"));
    }

    {
        // very narrow terminal (edge case: width < 4)
        WelcomeCell cell("0.1.0", "m", "b");
        Buffer buf;
        buf.resize(3, 40);
        cell.render(buf, 3);
        auto rendered = buffer_to_lines(buf);
        assert(rendered.size() == 10);
    }

    return 0;
}
