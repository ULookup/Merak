#pragma once
#include "external_editor.hpp"
#include "mention_menu.hpp"
#include "text_area.hpp"
#include "../../commands/command_registry.hpp"
#include "../history_cell/history_cell.hpp"
#include "../buffer.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <set>
#include <utility>
#include <vector>

namespace merak::tui {

struct SlashRow {
    std::string name;
    std::string completion;
    std::string description;
    commands::CommandGroup group = commands::CommandGroup::Core;
    std::vector<size_t> highlights;
    int score = 0;
};

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

    static std::string first_line(std::string_view text) {
        auto end = text.find('\n');
        return std::string(text.substr(0, end == std::string_view::npos ? text.size() : end));
    }
    static std::string lower_ascii(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    static int match_score(const std::string& needle, const std::string& haystack,
                           std::vector<size_t>* highlights = nullptr) {
        auto n = lower_ascii(needle);
        auto h = lower_ascii(haystack);
        if (highlights) highlights->clear();
        if (n.empty()) return 1;
        if (h == n) {
            if (highlights) for (size_t i = 0; i < haystack.size(); ++i) highlights->push_back(i);
            return 20000 - static_cast<int>(haystack.size());
        }
        if (h.starts_with(n)) {
            if (highlights) for (size_t i = 0; i < n.size() && i < haystack.size(); ++i) highlights->push_back(i);
            return 12000 - static_cast<int>(haystack.size());
        }
        auto found = h.find(n);
        if (found != std::string::npos) {
            if (highlights) for (size_t i = 0; i < n.size() && found + i < haystack.size(); ++i) highlights->push_back(found + i);
            return 8000 - static_cast<int>(found);
        }
        size_t at = 0;
        int score = 0;
        for (char c : n) {
            auto next = h.find(c, at);
            if (next == std::string::npos) return 0;
            if (highlights && next < haystack.size()) highlights->push_back(next);
            score += 20 - static_cast<int>(std::min<size_t>(next - at, 15));
            at = next + 1;
        }
        return score;
    }
    static std::vector<Span> highlighted_name(const SlashRow& row, const Style& base,
                                              const Style& hit, size_t min_width) {
        std::vector<Span> spans;
        std::set<size_t> hits(row.highlights.begin(), row.highlights.end());
        std::string chunk;
        bool chunk_hit = false;
        bool have_chunk = false;
        for (size_t i = 0; i < row.name.size(); ++i) {
            bool is_hit = hits.contains(i);
            if (have_chunk && is_hit != chunk_hit) {
                spans.push_back({chunk, chunk_hit ? hit : base});
                chunk.clear();
            }
            chunk.push_back(row.name[i]);
            chunk_hit = is_hit;
            have_chunk = true;
        }
        if (!chunk.empty()) spans.push_back({chunk, chunk_hit ? hit : base});
        if (row.name.size() < min_width) spans.push_back({std::string(min_width - row.name.size(), ' '), base});
        return spans;
    }

public:
    ChatComposer() { load_history(); }
    bool empty() const { return textarea_.empty(); }
    const std::string& text() const { return textarea_.text(); }
    size_t cursor() const { return textarea_.cursor(); }
    size_t cursor_col_in_line() const {
        size_t cur = textarea_.cursor();
        if (cur == 0) return 0;
        auto start = textarea_.text().rfind('\n', cur == 0 ? 0 : cur - 1);
        if (start == std::string::npos) return cur;
        return cur - start - 1;
    }
    size_t cursor_line() const {
        size_t cur = textarea_.cursor();
        size_t line = 0;
        for (size_t i = 0; i < cur; ++i) {
            if (textarea_.text()[i] == '\n') ++line;
        }
        return line;
    }
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

    bool slash_open() const {
        auto line = first_line(text());
        if (line.empty() || line[0] != '/') return false;
        auto space = line.find_first_of(" \t");
        if (space == std::string::npos) return true;
        auto parent_name = line.substr(0, space);
        auto* parent = commands::find_command(parent_name);
        if (!parent || parent->subcommands.empty()) return false;
        auto filter = line.substr(space + 1);
        while (!filter.empty() && std::isspace(static_cast<unsigned char>(filter.front()))) filter.erase(filter.begin());
        return filter.find_first_of(" \t") == std::string::npos;
    }
    std::vector<SlashRow> slash_matches(size_t limit = 14) const {
        if (!slash_open()) return {};
        auto line = first_line(text());
        auto space = line.find_first_of(" \t");
        std::vector<SlashRow> rows;
        if (space != std::string::npos) {
            auto parent_name = line.substr(0, space);
            auto filter = line.substr(space + 1);
            while (!filter.empty() && std::isspace(static_cast<unsigned char>(filter.front()))) filter.erase(filter.begin());
            auto* parent = commands::find_command(parent_name);
            if (!parent || parent->subcommands.empty()) return {};
            for (const auto& sub : parent->subcommands) {
                SlashRow row;
                row.name = sub.token;
                row.completion = parent->name + " " + sub.token + " ";
                row.description = sub.description;
                row.group = parent->group;
                row.score = match_score(filter, sub.token + " " + sub.description, &row.highlights);
                if (row.score > 0) rows.push_back(std::move(row));
            }
        } else {
            auto filter = line;
            if (!filter.empty() && filter[0] == '/') filter.erase(filter.begin());
            for (const auto& cmd : commands::all_commands()) {
                SlashRow row;
                row.name = cmd.name;
                row.completion = cmd.name + " ";
                row.description = cmd.description;
                row.group = cmd.group;
                auto searchable = cmd.name.substr(1) + " " + cmd.description;
                row.score = match_score(filter, searchable, &row.highlights);
                for (auto& h : row.highlights) ++h;
                if (row.score > 0) rows.push_back(std::move(row));
            }
        }
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            if (a.score != b.score) return a.score > b.score;
            if (a.group != b.group) return static_cast<int>(a.group) < static_cast<int>(b.group);
            return a.name < b.name;
        });
        if (rows.size() > limit) rows.resize(limit);
        return rows;
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
        textarea_.set_text(matches[slash_selected_].completion);
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
        if (!matches.empty() && first_line(text()).find_first_of(" \t") == std::string::npos) {
            if (slash_selected_ >= static_cast<int>(matches.size())) slash_selected_ = 0;
            result = matches[slash_selected_].name;
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

    void render(Buffer& buf) const {
        auto& t = theme::active_theme();
        Style accent; accent.fg = t.accent;
        Style warning_fg; warning_fg.fg = t.warn;
        Style dim_fg; dim_fg.fg = t.dim; dim_fg.dim(true);
        Style cursor_style; cursor_style.fg = t.accent;
        Style base;
        std::vector<std::vector<Span>> lines;

        auto text_lines = textarea_.lines();
        for (size_t i = 0; i < text_lines.size(); ++i) {
            std::vector<Span> line;
            Style prompt_style = submit_flash_ ? warning_fg : accent;
            line.push_back({i == 0 ? "> " : "  ", prompt_style});
            line.push_back({sanitize_terminal_text(text_lines[i]), base});
            if (i + 1 == text_lines.size()) line.push_back({"▎", cursor_style});
            lines.push_back(line);
        }
        if (empty()) {
            lines.push_back({Span{"> ", accent},
                             Span{"Ask merak to do anything", dim_fg},
                             Span{"▎", cursor_style}});
        }
        auto matches = slash_matches(32);
        if (slash_open()) {
            static constexpr size_t kVisibleRows = 10;
            if (matches.empty()) {
                lines.push_back({Span{"  no matching commands - try a shorter prefix", dim_fg}});
            }
            if (slash_selected_ >= static_cast<int>(matches.size())) slash_selected_ = 0;
            size_t selected = matches.empty() ? 0 : static_cast<size_t>(slash_selected_);
            size_t start = 0;
            if (matches.size() > kVisibleRows) {
                start = selected > kVisibleRows / 2 ? selected - kVisibleRows / 2 : 0;
                if (start + kVisibleRows > matches.size()) start = matches.size() - kVisibleRows;
            }
            size_t end = std::min(matches.size(), start + kVisibleRows);
            if (start > 0) lines.push_back({Span{"  ↑ " + std::to_string(start) + " more", dim_fg}});
            std::optional<commands::CommandGroup> last_group;
            size_t name_width = 0;
            for (size_t i = start; i < end; ++i) name_width = std::max(name_width, matches[i].name.size());
            const bool show_desc = buf.w >= 34;
            for (size_t i = start; i < end; ++i) {
                const auto& row = matches[i];
                if (last_group != row.group) {
                    last_group = row.group;
                    lines.push_back({Span{"  -- " + std::string(commands::group_name(row.group)) + " --", dim_fg}});
                }
                const bool selected_row = i == selected;
                Style name_style = selected_row ? accent : dim_fg;
                Style hit_style = accent; hit_style.underline(true); if (selected_row) hit_style.bold(true);
                Style desc_style = selected_row ? Style{} : dim_fg;
                if (selected_row) {
                    name_style.bg = t.selected_bg;
                    name_style.fg = t.selected_fg;
                    hit_style.bg = t.selected_bg;
                    hit_style.fg = t.selected_fg;
                    desc_style.bg = t.selected_bg;
                    desc_style.fg = t.selected_fg;
                }
                std::vector<Span> row_spans;
                row_spans.push_back({selected_row ? "  ▶ " : "    ", selected_row ? name_style : dim_fg});
                auto name_spans = highlighted_name(row, name_style, hit_style, show_desc ? name_width : 0);
                row_spans.insert(row_spans.end(), name_spans.begin(), name_spans.end());
                if (show_desc) {
                    row_spans.push_back({"  ", selected_row ? name_style : dim_fg});
                    row_spans.push_back({truncate_text(row.description, std::max<size_t>(8, buf.w > name_width + 8 ? buf.w - name_width - 8 : 8)), desc_style});
                }
                lines.push_back(std::move(row_spans));
            }
            if (end < matches.size()) lines.push_back({Span{"  ↓ " + std::to_string(matches.size() - end) + " more", dim_fg}});
        }
        if (mention_menu_.open()) {
            for (const auto& match : mention_menu_.matches()) {
                lines.push_back({Span{"    @" + match.path, dim_fg}});
            }
        }

        size_t total_lines = lines.size();
        if (buf.h < total_lines) buf.resize(buf.w, total_lines);
        write_spans(buf, lines);
    }
};

} // namespace merak::tui
