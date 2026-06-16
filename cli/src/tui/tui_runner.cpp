#include "tui_runner.hpp"
#include "../client/runtime_client.hpp"
#include "../commands/worldbuilding_commands.hpp"
#include "screen_manager.hpp"
#include "history_cell/welcome_cell.hpp"
#include "persistence/transcript.hpp"
#include "composer/external_editor.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <iostream>

namespace merak::tui {

namespace {

ToolCall tool_call(const nlohmann::json& p) {
    return {p.value("id", p.value("tool_call_id", "")),
            p.value("name", p.value("tool", "")),
            p.value("arguments", "")};
}

ToolResult tool_result(const nlohmann::json& p) {
    return {p.value("id", ""), p.value("output", ""), p.value("is_error", false)};
}

bool is_worldbuilding_input(const std::string& input) {
    return input.rfind("/world", 0) == 0
        || input.rfind("/agent", 0) == 0
        || input.rfind("/story", 0) == 0
        || input.rfind("/chapter", 0) == 0
        || input.rfind("/arc", 0) == 0
        || input.rfind("/scene", 0) == 0
        || input.rfind("/time", 0) == 0
        || input.rfind("/foreshadow", 0) == 0
        || input.rfind("/secret", 0) == 0
        || input.rfind("/voice", 0) == 0
        || input.rfind("/memory", 0) == 0
        || input.rfind("/diary", 0) == 0
        || input.rfind("@", 0) == 0;
}

bool command_requires_world(commands::WorldbuildingAction action) {
    using A = commands::WorldbuildingAction;
    switch (action) {
    case A::WorldList: case A::WorldCreate: case A::WorldUse:
    case A::WorldDelete: case A::None:
        return false;
    default:
        return true;
    }
}

bool is_context_switch(commands::WorldbuildingAction action) {
    using A = commands::WorldbuildingAction;
    return action == A::WorldUse || action == A::ChapterUse || action == A::SceneUse;
}

std::string normalize_team_pattern(std::string pattern) {
    if (pattern == "fanout") return "fan_out";
    return pattern;
}

std::vector<std::string> split_csv(std::string value) {
    std::vector<std::string> out;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item.erase(item.begin(), std::find_if(item.begin(), item.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        item.erase(std::find_if(item.rbegin(), item.rend(),
            [](unsigned char c) { return !std::isspace(c); }).base(), item.end());
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

} // anonymous namespace

void run_tui(const std::string& server_url,
             const std::string& initial_session_id)
{
    client::RuntimeClient api(server_url);
    auto meta = api.metadata();

    theme::configure_theme(theme::load_theme_from_metadata(
        meta.value("tui", nlohmann::json::object())
            .value("theme", nlohmann::json::object())));

    tui::ScreenManager ui;
    ui.set_runtime_metadata(meta);
    ui.status_bar().set_provider(meta.value("provider", "none"));
    ui.status_bar().set_model(meta.value("model", "none"));
    ui.status_bar().set_cwd(std::filesystem::current_path().string());
    {
        auto branch = tui::ExternalEditorResolver::shell_output(
            "git branch --show-current 2>/dev/null");
        ui.status_bar().set_git_branch(branch);
    }

    std::string session_id = initial_session_id;
    if (session_id.empty())
        session_id = api.create_session()["session_id"];
    else
        api.session(session_id);

    {
        auto sj = api.session(session_id);
        tui::persistence::SessionMeta m;
        m.session_id  = session_id;
        m.title       = sj.value("title", "");
        m.created_at  = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        m.model = meta.value("model", "none");
        m.cwd   = std::filesystem::current_path().string();
        tui::persistence::update_index(session_id, m);
    }

    // Shared state between SSE stream thread and UI thread
    std::mutex state_mutex;
    std::string current_run;
    long long last_seq = 0;
    std::atomic<bool> stop_stream = false;
    std::string current_world_id, current_chapter_id, current_scene_id;
    std::thread stream;

    // SSE event handler — runs on stream thread, posts UI updates
    auto apply = [&](client::SseFrame frame, bool recovering = false) {
        std::lock_guard lock(state_mutex);
        last_seq = std::max(last_seq, frame.seq);
        auto e  = frame.payload;
        auto type = e.value("type", frame.type);
        auto p    = e.value("payload", nlohmann::json::object());

        if (type == "run_started") {
            current_run = e.value("run_id", "");
            if (recovering)
                ui.post([&ui, text = p.value("message", "")] {
                    ui.timeline().submit_user(text);
                });
        } else if (type == "text_delta") {
            ui.post([&ui, text = p.value("text", "")] {
                ui.timeline().append_assistant(text);
            });
        } else if (type == "state_changed") {
            ui.post([&ui, state = p.value("to", "")] {
                ui.status_bar().set_state(state);
            });
        } else if (type == "usage_updated") {
            ui.post([&ui, p] {
                ui.record_usage(p.value("input_tokens", 0),
                                p.value("output_tokens", 0),
                                p.value("exact", false));
            });
        } else if (type == "tool_started") {
            auto c = tool_call(p);
            ui.post([&ui, c] {
                ui.record_tool_start();
                ui.timeline().start_tool(c);
            });
        } else if (type == "tool_completed") {
            auto r = tool_result(p);
            ui.post([&ui, r] {
                ui.record_tool_end();
                ui.timeline().finish_tool(r);
            });
        } else if (type == "delegation_started") {
            ui.post([&ui, p] {
                auto agents = p.value("agent_ids", nlohmann::json::array());
                ui.timeline().add_system(
                    "Team " + p.value("pattern", "") + " started with "
                    + std::to_string(agents.size()) + " agents");
            });
        } else if (type == "sub_run_started") {
            ui.post([&ui, p] {
                ui.record_agent_start();
                ui.timeline().add_system("Agent " + p.value("agent_id", "") + " started");
            });
        } else if (type == "sub_run_completed") {
            ui.post([&ui, p] {
                ui.record_agent_end();
                ui.timeline().add_system(
                    "Agent " + p.value("agent_id", "") + " "
                    + p.value("status", "completed"));
            });
        } else if (type == "delegation_completed") {
            ui.post([&ui, p] {
                ui.timeline().commit_active();
                auto output = p.value("aggregated_output", "");
                if (!output.empty()) ui.timeline().append_assistant(output);
                ui.timeline().commit_active();
                ui.timeline().add_system(
                    "Team completed · " + p.value("status", "completed"));
            });
        } else if (type == "approval_requested") {
            auto c = tool_call(p);
            auto approval = p.value("approval_id", "");
            ui.post([&ui, &api, c, approval] {
                ui.request_approval(c, [&api, approval](bool allow) {
                    api.resolve_approval(approval, allow);
                });
            });
        } else if (type == "run_completed" || type == "run_failed"
                || type == "run_cancelled" || type == "run_interrupted") {
            current_run.clear();
            ui.post([&ui, type, p] {
                ui.timeline().commit_active();
                if (type == "run_failed")
                    ui.timeline().add_system(
                        p.value("error", "Run failed"), true);
                ui.status_bar().set_state(
                    type == "run_cancelled" ? "Cancelled" : "Idle");
                ui.finish_remote_run();
            });
        }
    };

    auto start_stream = [&] {
        stop_stream = false;
        stream = std::thread([&] {
            while (!stop_stream) {
                long long cursor;
                {
                    std::lock_guard lock(state_mutex);
                    cursor = last_seq;
                }
                api.stream_events(session_id, cursor,
                    [&](auto f) { apply(std::move(f)); }, stop_stream);
                if (!stop_stream)
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
        });
    };

    // Replay existing events (recovery)
    for (auto& e : api.events(session_id)["events"])
        apply({e.value("seq", 0LL), e.value("type", ""), e}, true);

    start_stream();

    ui.timeline().commit(std::make_shared<tui::WelcomeCell>(
        "0.1.0", meta.value("model", "none"), ui.status_bar().git_branch()));
    ui.timeline().add_system("Connected to merak serve · session " + session_id);

    // Cancel handler
    ui.set_on_cancel([&] {
        std::string run;
        {
            std::lock_guard lock(state_mutex);
            run = current_run;
        }
        if (!run.empty()) api.cancel_run(run);
    });

    // Command handler
    ui.set_on_command([&](std::string input) {
        if (input == "/exit" || input == "/quit") { ui.exit(); return; }
        if (input == "/help")  { ui.open_help(); return; }

        if (is_worldbuilding_input(input)) {
            auto wb_cmd = commands::parse_worldbuilding_command(
                input, current_world_id, current_chapter_id, current_scene_id);
            if (wb_cmd && wb_cmd->action != commands::WorldbuildingAction::None) {
                auto wb_meta = meta.value("worldbuilding", nlohmann::json::object());
                bool wb_enabled = wb_meta.value("enabled", false);

                if (is_context_switch(wb_cmd->action)) {
                    if (wb_cmd->args.empty() || wb_cmd->args[0].empty()) {
                        ui.timeline().add_system(
                            "Usage: select an id, for example /world use <id>", true);
                        return;
                    }
                    if (wb_cmd->action == commands::WorldbuildingAction::WorldUse) {
                        current_world_id = wb_cmd->args[0];
                        current_chapter_id.clear();
                        current_scene_id.clear();
                        ui.timeline().add_system("Using world " + current_world_id);
                        return;
                    }
                    if (wb_cmd->action == commands::WorldbuildingAction::ChapterUse) {
                        if (current_world_id.empty()) {
                            ui.timeline().add_system(
                                "Select a world first with /world use <id>", true);
                            return;
                        }
                        current_chapter_id = wb_cmd->args[0];
                        current_scene_id.clear();
                        ui.timeline().add_system("Using chapter " + current_chapter_id);
                        return;
                    }
                    if (wb_cmd->action == commands::WorldbuildingAction::SceneUse) {
                        if (current_world_id.empty()) {
                            ui.timeline().add_system(
                                "Select a world first with /world use <id>", true);
                            return;
                        }
                        current_scene_id = wb_cmd->args[0];
                        ui.timeline().add_system("Using scene " + current_scene_id);
                        return;
                    }
                }
                if (command_requires_world(wb_cmd->action) && current_world_id.empty()) {
                    ui.timeline().add_system(
                        "Select a world first with /world use <id>", true);
                    return;
                }
                if (!wb_enabled) {
                    ui.timeline().add_system(
                        "Worldbuilding API is not available. Enable memory.db_connection "
                        "or start merak serve with bundled PostgreSQL.", true);
                    return;
                }
                auto result = commands::execute_worldbuilding_command(*wb_cmd,
                    [&api](const std::string& method, const std::string& path,
                           const nlohmann::json& body) {
                        return api.request(method, path, body);
                    });
                auto formatted = tui::SystemCell::format_worldbuilding_result(result);
                ui.timeline().add_system(formatted,
                    formatted.rfind("Error:", 0) == 0);
                return;
            }
        }

        if (input == "/context")        { ui.open_context(); return; }
        if (input == "/model" || input.rfind("/model ", 0) == 0) {
            ui.open_model_selector(); return;
        }
        if (input == "/transcript")     { ui.open_transcript(); return; }
        if (input == "/tool-calls")     { ui.open_tool_browser(); return; }
        if (input == "/agents") {
            std::ostringstream out;
            out << "Agents";
            for (auto& a : meta.value("agents", nlohmann::json::array()))
                out << "\n" << a.value("id", "") << "  " << a.value("description", "");
            ui.timeline().add_system(out.str());
            return;
        }
        if (input.rfind("/team ", 0) == 0) {
            std::istringstream parts(input.substr(6));
            std::string pattern, agent_list;
            parts >> pattern >> agent_list;
            std::string task;
            std::getline(parts, task);
            while (!task.empty() && std::isspace(
                static_cast<unsigned char>(task.front()))) task.erase(task.begin());
            auto agents = split_csv(agent_list);
            pattern = normalize_team_pattern(pattern);
            if ((pattern != "fan_out" && pattern != "sequential"
                 && pattern != "pipeline") || agents.empty() || task.empty()) {
                ui.timeline().add_system(
                    "Usage: /team fanout|sequential|pipeline agent1,agent2 task", true);
                return;
            }
            ui.timeline().submit_user(input);
            ui.start_background([&api, &ui, &session_id, pattern, agents, task] {
                try {
                    api.start_delegation(session_id, pattern, agents, task);
                } catch (const std::exception& e) {
                    ui.post([&ui, error = std::string(e.what())] {
                        ui.timeline().add_system(error, true);
                        ui.finish_remote_run();
                    });
                }
            });
            return;
        }
        if (input == "/tools") { ui.open_tools(); return; }
        if (input == "/memory" || input.rfind("/memory ", 0) == 0) {
            try {
                ui.set_memory_items(
                    api.memory(session_id).value("items", nlohmann::json::array()));
            } catch (const std::exception& e) {
                ui.timeline().add_system(
                    std::string("Memory refresh failed: ") + e.what(), true);
            }
            ui.open_memory();
            return;
        }
        if (input == "/session list") {
            auto sessions = api.list_sessions()["sessions"];
            std::ostringstream out;
            out << "Sessions";
            for (auto& s : sessions) {
                std::string title = s.value("title", "");
                out << "\n" << (title.empty() ? "New Session" : title)
                    << "  [" << s["id"] << "]";
            }
            ui.timeline().add_system(out.str());
            return;
        }
        if (input.rfind("/session rename ", 0) == 0) {
            std::string new_title = input.substr(16);
            while (!new_title.empty() && std::isspace(
                static_cast<unsigned char>(new_title.front())))
                new_title.erase(new_title.begin());
            if (new_title.empty()) {
                ui.timeline().add_system("Usage: /session rename <new title>", true);
            } else {
                api.request("PATCH", "/v1/sessions/" + session_id,
                            {{"title", new_title}});
                ui.timeline().add_system("Session renamed to: " + new_title);
            }
            return;
        }
        if (input == "/clear") input = "/session new";
        if (input.rfind("/session new", 0) == 0
            || input.rfind("/session use ", 0) == 0) {
            std::string title;
            if (input.rfind("/session new", 0) == 0) {
                auto pos = input.find("--title");
                if (pos != std::string::npos) title = input.substr(pos + 8);
                while (!title.empty() && std::isspace(
                    static_cast<unsigned char>(title.front())))
                    title.erase(title.begin());
            }
            if (ui.busy() || ui.queued_messages() > 0) {
                ui.timeline().add_system(
                    "Cannot switch sessions while a run or queued message is active",
                    true);
                return;
            }
            stop_stream = true;
            if (stream.joinable()) stream.join();

            if (input.rfind("/session new", 0) == 0) {
                auto sj = api.create_session(title);
                session_id = sj["session_id"];
                tui::persistence::SessionMeta m;
                m.session_id = session_id;
                m.title = sj.value("session", nlohmann::json::object())
                    .value("title", "");
                m.created_at = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                m.model = meta.value("model", "none");
                m.cwd   = std::filesystem::current_path().string();
                tui::persistence::update_index(session_id, m);
            } else {
                session_id = input.substr(13);
                auto sj = api.session(session_id);
                tui::persistence::SessionMeta m;
                m.session_id = session_id;
                m.title = sj.value("title", "");
                m.created_at = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                m.model = meta.value("model", "none");
                m.cwd   = std::filesystem::current_path().string();
                tui::persistence::update_index(session_id, m);
            }
            {
                std::lock_guard lock(state_mutex);
                last_seq = 0;
                current_run.clear();
            }
            ui.reset_timeline();
            for (auto& e : api.events(session_id)["events"])
                apply({e.value("seq", 0LL), e.value("type", ""), e}, true);
            start_stream();
            ui.timeline().add_system("Using session " + session_id);
            return;
        }
        if (input.starts_with("/")) {
            ui.timeline().add_system("Unknown command: " + input, true);
            return;
        }

        // Plain user message — start a run
        auto model = ui.selected_model();
        ui.start_background([&api, &ui, &session_id, input = std::move(input), model] {
            try {
                api.start_run(session_id, input, model);
            } catch (const std::exception& e) {
                ui.post([&ui, error = std::string(e.what())] {
                    ui.timeline().add_system(error, true);
                    ui.finish_remote_run();
                });
            }
        });
    });

    ui.run();
    stop_stream = true;
    if (stream.joinable()) stream.join();
}

} // namespace merak::tui
