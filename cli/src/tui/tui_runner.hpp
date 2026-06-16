#pragma once
#include <string>

namespace merak::tui {

// Launch the interactive TUI connected to a merak serve instance.
// If session_id is empty, a new session will be created automatically.
void run_tui(const std::string& server_url,
             const std::string& initial_session_id = "");

} // namespace merak::tui
