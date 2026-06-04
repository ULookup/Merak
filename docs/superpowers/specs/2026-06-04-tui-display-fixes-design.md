# TUI Display & Input Fixes — Design Spec

**Date**: 2026-06-04
**Branch**: (to be created from feature/tui-worldbuilding-integration)

## Overview

Fix four TUI issues: markdown bold rendering, CJK character display, /world HTTP error, and IME cursor positioning.

## Issue 1: CJK Characters Display as Blocks/Question Marks

**Root cause**: `sanitize_terminal_text()` in `cli/src/tui/history_cell.hpp` filters all bytes >= 0x80, destroying UTF-8 multi-byte sequences.

**Fix**: Rewrite `sanitize_terminal_text()` to preserve valid UTF-8 sequences.

- Add inline helper `utf8_sequence_length(unsigned char lead)` — returns 1-4 for valid lead bytes, 0 for invalid
- In the main loop, when a byte >= 0x80 is encountered, decode the full UTF-8 sequence, validate continuation bytes, and copy the entire sequence to output if valid
- Non-printable ASCII (0x00-0x1F, 0x7F, 0x9B) and ANSI escape sequences (`\x1b[...`) continue to be stripped
- File: `cli/src/tui/history_cell.hpp`

## Issue 2: Markdown `**bold**` Not Rendering

**Root cause**: `AssistantCell::render_inline()` checks for `* ` (list item) before checking `**bold**`, so lines like `* **text**` skip inline parsing. The toggle-based `**` parser also has edge-case bugs.

**Fix**: Reorder `render_inline()` logic:

1. Process headings (`#`, `##`) first — return immediately
2. Process blockquote (`> `) — strip prefix, then inline-parse the rest
3. Parse inline styles on the full line first: `` `code` ``, `**bold**`, `__italic__`
4. THEN check if original line starts with `* ` or `- ` for list bullet replacement
5. Fix toggle edge cases: consecutive `****`, `**` at position 0, unclosed markers

- File: `cli/src/tui/history_cell.hpp`

## Issue 3: /world Shows "HTTP error"

**Root cause**: In `RuntimeClient::request()` (`cli/src/client/runtime_client.cpp:10`), error JSON is parsed as `json.value("error", {}).value("message", "HTTP error")`, but the server returns `{"ok": false, "error": "plain string"}`, so the real message is never extracted.

**Fix**: Change to `json.value("error", "HTTP error")` — directly read the error string.

- File: `cli/src/client/runtime_client.cpp`

## Issue 4: Chinese IME Cursor Not in Input Box

**Root cause**: Three compounding problems:
- `TerminalEventReader::next()` reads bytes one-at-a-time; UTF-8 multi-byte sequences are split into separate Character events, each inserting garbage
- `ChatComposer::insert_char(char c)` only accepts single bytes
- Terminal in raw mode doesn't position cursor for IME composition window

**Fix** (3 files):

### 4a. `TerminalEventReader` — UTF-8 byte sequence decoding

- In the Character branch of `next()`, when the byte is a UTF-8 lead byte (>= 0xC0), read continuation bytes and assemble a complete code point
- Emit as `TerminalEvent::Type::Character` with `text` field populated
- File: `cli/src/tui/terminal_event_reader.hpp`

### 4b. `ChatComposer` — accept multi-byte text

- Change `insert_char(char c)` to `insert_text(std::string_view text)` 
- Directly calls `textarea_.insert(text)`
- Update all call sites in `ScreenManager::handle_event()` to use `event.text` instead of `event.character`
- File: `cli/src/tui/composer/chat_composer.hpp`

### 4c. `InlineTerminal` — cursor positioning for IME

- In `redraw()`, after rendering the frame, emit cursor position at the end of the input area line
- Use CPR (Cursor Position Request) escape sequence
- File: `cli/src/tui/inline_terminal.hpp`

## Files Changed

| File | Changes |
|------|---------|
| `cli/src/tui/history_cell.hpp` | UTF-8 sanitization rewrite, markdown rendering fix |
| `cli/src/client/runtime_client.cpp` | Error message parsing fix |
| `cli/src/tui/terminal_event_reader.hpp` | UTF-8 sequence decoding in event reader |
| `cli/src/tui/composer/chat_composer.hpp` | `insert_char` → `insert_text` |
| `cli/src/tui/inline_terminal.hpp` | Cursor position for IME |
| `cli/src/tui/screen_manager.hpp` | Adapt to `event.text` for Character events |
