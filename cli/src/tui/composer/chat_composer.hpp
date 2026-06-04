#pragma once
#include "external_editor.hpp"
#include "mention_menu.hpp"
#include "text_area.hpp"
#include "../../commands/command_registry.hpp"
#include "../history_cell/history_cell.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace merak::tui {

class ChatComposer {
    TextArea textarea_;
    std::vector<std::string> history_;
    std::optional<size_t> history_index_;
    std::string draft_;
    std::vector<std::pair<std::string, std::string>> pasted_;
    unsigned paste_counter_ = 0;
    int slash_selected_ = 0;
    MentionMenu mention_menu_;
    bool submit_flash_ = false;

    static std::filesystem::path history_path() {
        auto home = std::getenv("HOME");
        return (home ? std::filesystem::path(home) : std::filesystem::path("."))
            / ".merak" / "history";
    }
    void load_history() {
        std::ifstream input(history_path());
        std::string line;
        while (std::getline(input, line)) {
            size_t pos = 0;
            while ((pos = line.find("\\n", pos)) != std::string::npos) {
                line.replace(pos, 2, "\n");
                ++pos;
            }
            if (!line.empty()) history_.push_back(line);
        }
        if (history_.size() > 500) history_.erase(history_.begin(), history_.end() - 500);
    }
    void append_history(const std::string& text) {
        if (text.empty() || (!history_.empty() && history_.back() == text)) return;
        history_.push_back(text);
        try {
            std::filesystem::create_directories(history_path().parent_path());
            std::ofstream output(history_path(), std::ios::app);
            auto encoded = text;
            size_t pos = 0;
            while ((pos = encoded.find('\n', pos)) != std::string::npos) {
                encoded.replace(pos, 1, "\\n");
                pos += 2;
            }
            output << encoded << '\n';
            output.close();
            if (history_.size() > 500) {
                history_.erase(history_.begin(), history_.end() - 500);
                std::ofstream rotated(history_path());
                for (auto entry : history_) {
                    size_t line_pos = 0;
                    while ((line_pos = entry.find('\n', line_pos)) != std::string::npos) {
                        entry.replace(line_pos, 1, "\\n");
                        line_pos += 2;
                    }
                    rotated << entry << '\n';
                }
            }
        } catch (...) {
        }
    }
    void refresh_mention() { mention_menu_.update_from(textarea_.text(), textarea_.cursor()); }

public:
    ChatComposer() { load_history(); }
    bool empty() const { return textarea_.empty(); }
    const std::string& text() const { return textarea_.text(); }
    size_t cursor() const { return textarea_.cursor(); }
    void clear() { textarea_.clear(); history_index_.reset(); pasted_.clear(); }
    void set_text(std::string text) { textarea_.set_text(std::move(text)); }
    void replace_range(size_t start, size_t end, std::string_view value) { textarea_.replace_range(start, end, value); refresh_mention(); }
    void insert_text(std::string_view text) { textarea_.insert(text); slash_selected_ = 0; refresh_mention(); }
    void newline() { textarea_.newline(); refresh_mention(); }
    void backspace() { textarea_.backspace(); slash_selected_ = 0; refresh_mention(); }
    void delete_forward() { textarea_.delete_forward(); refresh_mention(); }
    void move_left() { textarea_.move_left(); refresh_mention(); }
    void move_right() { textarea_.move_right(); refresh_mention(); }
    void move_home() { textarea_.move_home(); refresh_mention(); }
    void move_end() { textarea_.move_end(); refresh_mention(); }
    void move_word_left() { textarea_.move_word_left(); refresh_mention(); }
    void move_word_right() { textarea_.move_word_right(); refresh_mention(); }
    void kill_to_start() { textarea_.kill_to_start(); refresh_mention(); }
    void kill_to_end() { textarea_.kill_to_end(); refresh_mention(); }
    void delete_word_left() { textarea_.delete_word_left(); refresh_mention(); }
    void yank() { textarea_.yank(); refresh_mention(); }
    bool open_external_editor() {
        auto edited = edit_text_external(textarea_.text());
        if (!edited) return false;
        textarea_.set_text(*edited);
        refresh_mention();
        return true;
    }
    bool mention_open() const { return mention_menu_.open(); }
    void mention_next() { mention_menu_.next(); }
    void mention_prev() { mention_menu_.prev(); }
    void mention_accept() {
        if (!mention_menu_.open()) return;
        textarea_.replace_range(mention_menu_.trigger_start(), textarea_.cursor(),
                                mention_menu_.accepted_text());
        refresh_mention();
    }
    void set_submit_flash(bool value) { submit_flash_ = value; }

    bool slash_open() const { return !text().empty() && text()[0] == '/' && text().find(' ') == std::string::npos; }
    std::vector<const commands::CommandMeta*> slash_matches() const {
        return slash_open() ? commands::fuzzy_match(text(), 8)
                            : std::vector<const commands::CommandMeta*>{};
    }
    void slash_next() {
        auto matches = slash_matches();
        if (!matches.empty()) slash_selected_ = (slash_selected_ + 1) % static_cast<int>(matches.size());
    }
    void slash_prev() {
        auto matches = slash_matches();
        if (!matches.empty()) slash_selected_ = (slash_selected_ + static_cast<int>(matches.size()) - 1)
            % static_cast<int>(matches.size());
    }
    void slash_complete() {
        if (mention_menu_.open()) { mention_accept(); return; }
        auto matches = slash_matches();
        if (matches.empty()) return;
        if (slash_selected_ >= static_cast<int>(matches.size())) slash_selected_ = 0;
        textarea_.set_text(matches[slash_selected_]->name + " ");
    }

    void history_prev() {
        if (history_.empty()) return;
        if (!history_index_) {
            draft_ = text();
            history_index_ = history_.size() - 1;
        } else if (*history_index_ > 0) {
            --*history_index_;
        }
        textarea_.set_text(history_[*history_index_]);
    }
    void history_next() {
        if (!history_index_) return;
        if (*history_index_ + 1 < history_.size()) {
            ++*history_index_;
            textarea_.set_text(history_[*history_index_]);
        } else {
            history_index_.reset();
            textarea_.set_text(draft_);
        }
    }
    void handle_paste(std::string value) {
        value = sanitize_terminal_text(value);
        const auto lines = static_cast<int>(std::count(value.begin(), value.end(), '\n')) + 1;
        if (value.size() <= 800 && lines <= 2) {
            textarea_.insert(value);
            return;
        }
        auto placeholder = "[Pasted #" + std::to_string(++paste_counter_) + " · "
            + std::to_string(lines) + " lines]";
        pasted_.push_back({placeholder, std::move(value)});
        textarea_.insert(placeholder);
    }
    std::string submit() {
        auto result = text();
        auto matches = slash_matches();
        if (!matches.empty()) {
            if (slash_selected_ >= static_cast<int>(matches.size())) slash_selected_ = 0;
            result = matches[slash_selected_]->name;
        }
        for (const auto& [placeholder, value] : pasted_) {
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.size(), value);
                pos += value.size();
            }
        }
        append_history(result);
        clear();
        return result;
    }

    std::vector<std::string> render() const {
        std::vector<std::string> lines;
        auto text_lines = textarea_.lines();
        for (size_t i = 0; i < text_lines.size(); ++i) {
            lines.push_back((i == 0 ? ansi(submit_flash_ ? theme::ANSI_WARNING : theme::ANSI_ACCENT, "› ") : "  ")
                + sanitize_terminal_text(text_lines[i])
                + (i + 1 == text_lines.size() ? ansi(theme::ANSI_ACCENT, "▎") : ""));
        }
        if (empty()) lines = {ansi(theme::ANSI_ACCENT, "› ")
            + ansi(theme::ANSI_DIM, "Ask merak to do anything") + ansi(theme::ANSI_ACCENT, "▎")};
        auto matches = slash_matches();
        for (size_t i = 0; i < matches.size(); ++i) {
            lines.push_back(std::string(i == static_cast<size_t>(slash_selected_) ? "  › " : "    ")
                + ansi(i == static_cast<size_t>(slash_selected_) ? theme::ANSI_ACCENT : theme::ANSI_DIM,
                       matches[i]->name + "  " + matches[i]->description));
        }
        if (mention_menu_.open()) {
            for (const auto& match : mention_menu_.matches()) {
                lines.push_back("    " + ansi(theme::ANSI_DIM, "@" + match.path));
            }
        }
        return lines;
    }
};

} // namespace merak::tui
