# TUI Display & Input Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four TUI issues: CJK character display, markdown bold rendering, /world HTTP error, and Chinese IME cursor positioning.

**Architecture:** Five files changed. UTF-8 sanitization and markdown parsing fixed in `history_cell.hpp`. HTTP error parsing fixed in `runtime_client.cpp`. Terminal event reader upgraded to decode multi-byte UTF-8 sequences. ChatComposer API changed from `insert_char(char)` to `insert_text(string_view)`. InlineTerminal positions cursor at composer for IME.

**Tech Stack:** C++17, nlohmann/json, no new dependencies.

**Spec:** `docs/superpowers/specs/2026-06-04-tui-display-fixes-design.md`

---

### Task 1: Create feature branch

- [ ] **Step 1: Create the branch**

```bash
git -C /home/icepop/Merak checkout -b fix/tui-display-issues
```

---

### Task 2: Fix UTF-8 sanitization — CJK characters display as blocks

**Files:**
- Modify: `cli/src/tui/history_cell.hpp:15-47`

**Why:** `sanitize_terminal_text()` filters out all bytes >= 0x80, destroying UTF-8 multi-byte sequences. Every CJK character becomes a block or question mark.

- [ ] **Step 1: Add `utf8_sequence_length` helper after the namespace opening (line 15)**

Insert after `namespace merak::tui {`:

```cpp
inline int utf8_sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}
```

- [ ] **Step 2: Replace `sanitize_terminal_text` (lines 17-47)**

Replace the entire function:

```cpp
inline std::string sanitize_terminal_text(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        auto c = static_cast<unsigned char>(input[i]);
        // Strip ANSI escape sequences
        if (c == 0x1b || c == 0x9b) {
            if (c == 0x1b && i + 1 < input.size() && input[i + 1] == ']') {
                i += 2;
                while (i < input.size()) {
                    if (input[i] == '\a') break;
                    if (input[i] == '\x1b' && i + 1 < input.size() && input[i + 1] == '\\') {
                        ++i; break;
                    }
                    ++i;
                }
                continue;
            }
            if (c == 0x1b && i + 1 < input.size() && input[i + 1] == '[') ++i;
            while (i + 1 < input.size()) {
                auto next = static_cast<unsigned char>(input[++i]);
                if (next >= 0x40 && next <= 0x7e) break;
            }
            continue;
        }
        // Preserve valid UTF-8 multi-byte sequences
        if (c >= 0x80) {
            int seq_len = utf8_sequence_length(c);
            if (seq_len > 1) {
                bool valid = true;
                for (int j = 1; j < seq_len; ++j) {
                    if (i + j >= input.size() ||
                        (static_cast<unsigned char>(input[i + j]) & 0xC0) != 0x80) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    for (int j = 0; j < seq_len; ++j) out.push_back(input[i + j]);
                    i += seq_len - 1;
                }
            }
            continue;
        }
        // Printable ASCII
        if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7f)) {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}
```

- [ ] **Step 3: Add test for UTF-8 sanitization**

In `cli/tests/test_tui_features.cpp`, add after the existing `AssistantCell` test block (line 36):

```cpp
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
```

- [ ] **Step 4: Build and run tests**

```bash
cd /home/icepop/Merak && make build && ./build/cli/tests/test_tui_features
```

- [ ] **Step 5: Commit**

```bash
git -C /home/icepop/Merak add cli/src/tui/history_cell.hpp cli/tests/test_tui_features.cpp
git -C /home/icepop/Merak commit -m "fix: preserve UTF-8 sequences in sanitize_terminal_text"
```

---

### Task 3: Fix markdown `**bold**` and `__italic__` inline rendering

**Files:**
- Modify: `cli/src/tui/history_cell.hpp:105-130` (`render_inline`)

**Why:** `* ` list-item detection runs before `**bold**` inline parsing. Toggle parser also has edge cases (consecutive `****`, unclosed markers).

- [ ] **Step 1: Replace `render_inline`**

Replace the function (lines 105-130):

```cpp
static std::string render_inline(const std::string& line) {
    // Headings
    if (line.starts_with("# ")) return ansi(theme::ANSI_BOLD, line.substr(2));
    if (line.starts_with("## ")) return ansi(theme::ANSI_BOLD, line.substr(3));

    // Blockquote: strip prefix, recursively inline-parse remainder
    if (line.starts_with("> ")) {
        return ansi(theme::ANSI_DIM, "│ ") + render_inline(line.substr(2));
    }

    // Inline parsing: `code`, **bold**, __italic__
    bool in_code = false;
    bool in_bold = false;
    bool in_italic = false;
    std::string out;
    for (size_t i = 0; i < line.size(); ++i) {
        auto c = line[i];
        if (c == '`') {
            out += in_code ? theme::ANSI_RESET : theme::ANSI_ACCENT;
            in_code = !in_code;
        } else if (c == '*' && i + 1 < line.size() && line[i + 1] == '*') {
            out += in_bold ? theme::ANSI_RESET : theme::ANSI_BOLD;
            in_bold = !in_bold;
            ++i;
        } else if (c == '_' && i + 1 < line.size() && line[i + 1] == '_') {
            out += in_italic ? theme::ANSI_RESET : theme::ANSI_ACCENT;
            in_italic = !in_italic;
            ++i;
        } else {
            out.push_back(c);
        }
    }
    if (in_code || in_bold || in_italic) out += theme::ANSI_RESET;

    // List items: replace leading "* " or "- " with bullet AFTER inline parsing
    if (line.starts_with("* ") || line.starts_with("- ")) {
        return ansi(theme::ANSI_ACCENT, "• ") + out.substr(2);
    }

    return out;
}
```

- [ ] **Step 2: Add render test in the existing AssistantCell test block**

In `cli/tests/test_tui_features.cpp`, replace the first test block (lines 22-36) to add bold and italic checks:

The existing test already creates an AssistantCell. Add these assertions inside that test block (after line 36, before the closing `}`):

```cpp
// Test bold rendering
{
    AssistantCell bold_cell;
    bold_cell.append("**bold text**");
    bold_cell.finalize();
    auto rendered = bold_cell.render(100);
    // The rendered output should contain ANSI bold codes and the text
    bool found = false;
    for (const auto& line : rendered) {
        if (line.find("bold text") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found);
}
// Test that list item with bold works
{
    AssistantCell list_bold;
    list_bold.append("* **bold item**\n");
    list_bold.finalize();
    auto rendered = list_bold.render(100);
    bool found_bold = false;
    for (const auto& line : rendered) {
        if (line.find("bold item") != std::string::npos) {
            found_bold = true;
            break;
        }
    }
    assert(found_bold);
}
```

- [ ] **Step 3: Build and run tests**

```bash
cd /home/icepop/Merak && make build && ./build/cli/tests/test_tui_features
```

- [ ] **Step 4: Commit**

```bash
git -C /home/icepop/Merak add cli/src/tui/history_cell.hpp cli/tests/test_tui_features.cpp
git -C /home/icepop/Merak commit -m "fix: reorder markdown inline rendering so bold/italic work with list items"
```

---

### Task 4: Fix `/world` HTTP error message parsing

**Files:**
- Modify: `cli/src/client/runtime_client.cpp:10`

**Why:** Server responds `{"ok": false, "error": "message"}`, but client tries `json.value("error", {}).value("message", "HTTP error")` which treats `"error"` as an object and never sees the string.

- [ ] **Step 1: Fix the error extraction (line 10)**

Change:
```cpp
if(status>=400)throw std::runtime_error(json.value("error",nlohmann::json::object()).value("message","HTTP error"));
```

To:
```cpp
if(status>=400)throw std::runtime_error(json.value("error","HTTP error"));
```

- [ ] **Step 2: Commit**

```bash
git -C /home/icepop/Merak add cli/src/client/runtime_client.cpp
git -C /home/icepop/Merak commit -m "fix: read error string directly from HTTP error response JSON"
```

---

### Task 5: Decode full UTF-8 sequences in TerminalEventReader

**Files:**
- Modify: `cli/src/tui/terminal_event_reader.hpp:89-93`

**Why:** `next()` reads one byte for Character events. CJK characters are 3 bytes — each byte fires a separate event, inserting garbage. The reader must assemble complete UTF-8 code points.

- [ ] **Step 1: Replace the Character branch (lines 89-93)**

Replace the `default` case in the switch:

```cpp
default:
    if (static_cast<unsigned char>(c) >= 0x20) {
        int seq_len = 1;
        if ((static_cast<unsigned char>(c) & 0x80) != 0) {
            if ((static_cast<unsigned char>(c) & 0xE0) == 0xC0) seq_len = 2;
            else if ((static_cast<unsigned char>(c) & 0xF0) == 0xE0) seq_len = 3;
            else if ((static_cast<unsigned char>(c) & 0xF8) == 0xF0) seq_len = 4;
            else return {}; // invalid lead byte
        }
        std::string utf8_char(1, c);
        for (int j = 1; j < seq_len; ++j) {
            char next;
            if (!read_byte(next, 5)) break;
            utf8_char.push_back(next);
        }
        return {TerminalEvent::Type::Character, 0, utf8_char};
    }
    return {};
```

- [ ] **Step 2: Commit**

```bash
git -C /home/icepop/Merak add cli/src/tui/terminal_event_reader.hpp
git -C /home/icepop/Merak commit -m "fix: decode full UTF-8 byte sequences in TerminalEventReader"
```

---

### Task 6: ChatComposer `insert_char` → `insert_text` + ScreenManager adapter

**Files:**
- Modify: `cli/src/tui/composer/chat_composer.hpp:86`
- Modify: `cli/src/tui/screen_manager.hpp:209`

**Why:** After Task 5, Character events carry multi-byte UTF-8 strings in `event.text`. `insert_char(char c)` only accepts single bytes. Must switch to `insert_text(string_view)`.

- [ ] **Step 1: Rename and change signature (chat_composer.hpp line 86)**

Replace:
```cpp
void insert_char(char c) { textarea_.insert_char(c); slash_selected_ = 0; refresh_mention(); }
```

With:
```cpp
void insert_text(std::string_view text) { textarea_.insert(text); slash_selected_ = 0; refresh_mention(); }
```

- [ ] **Step 2: Update ScreenManager call site (screen_manager.hpp line 209)**

Replace:
```cpp
if (event.type == Type::Character) composer_.insert_char(event.character);
```

With:
```cpp
if (event.type == Type::Character) composer_.insert_text(event.text);
```

- [ ] **Step 3: Commit**

```bash
git -C /home/icepop/Merak add cli/src/tui/composer/chat_composer.hpp cli/src/tui/screen_manager.hpp
git -C /home/icepop/Merak commit -m "fix: change insert_char to insert_text for multi-byte UTF-8 input"
```

---

### Task 7: Position terminal cursor at composer for IME

**Files:**
- Modify: `cli/src/tui/composer/chat_composer.hpp` (add cursor column method)
- Modify: `cli/src/tui/screen_manager.hpp:519` (emit cursor position after redraw)

**Why:** Terminal in raw mode hides cursor (`\x1b[?25l`). IME uses cursor position to place its composition window. We must briefly position the cursor at the composer input area each frame.

- [ ] **Step 1: Add `cursor_col_in_line()` to ChatComposer (after line 93, `move_end()`)**

```cpp
// Column of cursor within its current line (for IME cursor positioning)
size_t cursor_col_in_line() const {
    size_t cursor = textarea_.cursor();
    auto start = textarea_.text().rfind('\n', cursor == 0 ? 0 : cursor - 1);
    if (start == std::string::npos) start = 0;
    else start += 1; // after newline
    return cursor - start;
}
```

Wait — `TextArea` members are private. Need to either make this a TextArea method or access it through the ChatComposer's existing `cursor()` and `text()`.

Since `TextArea::cursor()` and `TextArea::text()` are public, compute from those:

```cpp
size_t cursor_col_in_line() const {
    size_t cur = textarea_.cursor();
    size_t start = textarea_.text().rfind('\n', cur == 0 ? 0 : cur - 1);
    if (start == std::string::npos) return cur;
    return cur - start - 1;
}
```

Insert this after line 82 (`size_t cursor() const { return textarea_.cursor(); }`):

```cpp
size_t cursor_col_in_line() const {
    size_t cur = textarea_.cursor();
    auto start = textarea_.text().rfind('\n', cur == 0 ? 0 : cur - 1);
    if (start == std::string::npos) return cur;
    return cur - start - 1;
}
```

- [ ] **Step 2: Emit cursor position in ScreenManager::run() (after line 519)**

In `screen_manager.hpp`, replace:
```cpp
terminal_.redraw(frame_lines());
```

With:
```cpp
auto frame = frame_lines();
terminal_.redraw(frame);
// Position cursor at composer for IME composition window
if (overlay_ == Overlay::None) {
    auto composer_lines = composer_.render();
    if (!composer_lines.empty()) {
        // composer is before the last 2 frame lines (help + status bar)
        // Cursor column = "› " prefix (2) + cursor column in current line
        size_t col = 2 + composer_.cursor_col_in_line();
        std::cout << "\x1b[" << (composer_lines.size() + 2) << "A\r\x1b[" << col << "C" << std::flush;
    }
}
```

- [ ] **Step 3: Commit**

```bash
git -C /home/icepop/Merak add cli/src/tui/composer/chat_composer.hpp cli/src/tui/screen_manager.hpp
git -C /home/icepop/Merak commit -m "fix: position terminal cursor at composer for IME input"
```

---

### Task 8: Full build and verify

- [ ] **Step 1: Clean build**

```bash
cd /home/icepop/Merak && make clean && make build
```

Expected: build succeeds with no errors.

- [ ] **Step 2: Run all tests**

```bash
./build/cli/tests/test_tui_features
./build/cli/tests/test_worldbuilding_commands
./build/cli/tests/test_runtime_client
```

Expected: all tests pass.

- [ ] **Step 3: Verify git log is clean**

```bash
git -C /home/icepop/Merak log --oneline -8
```

Should show 7 commits on `fix/tui-display-issues`.

---

### Files Changed Summary

| File | Task | Change |
|------|------|--------|
| `cli/src/tui/history_cell.hpp` | 2, 3 | UTF-8 sanitization + `utf8_sequence_length` + `render_inline` reorder |
| `cli/src/client/runtime_client.cpp` | 4 | Error message parsing fix |
| `cli/src/tui/terminal_event_reader.hpp` | 5 | UTF-8 sequence decoding |
| `cli/src/tui/composer/chat_composer.hpp` | 6, 7 | `insert_char` → `insert_text` + `cursor_col_in_line()` |
| `cli/src/tui/screen_manager.hpp` | 6, 7 | `event.text` adapter + IME cursor positioning |
| `cli/tests/test_tui_features.cpp` | 2, 3 | UTF-8 + bold rendering tests |
