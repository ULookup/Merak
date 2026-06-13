use serde::Serialize;
use std::sync::{Arc, Mutex};
use tauri::Manager;

mod runtime;

#[derive(Serialize)]
struct AppInfo {
    name: &'static str,
    version: &'static str,
    platform: &'static str,
}

#[tauri::command]
fn app_info() -> AppInfo {
    AppInfo {
        name: "Merak",
        version: env!("CARGO_PKG_VERSION"),
        platform: std::env::consts::OS,
    }
}

#[tauri::command]
fn runtime_status(state: tauri::State<'_, runtime::SharedRuntime>) -> runtime::RuntimeStatus {
    runtime::runtime_status(state.inner())
}

#[tauri::command]
fn runtime_restart(
    state: tauri::State<'_, runtime::SharedRuntime>,
) -> runtime::RuntimeRestartResponse {
    runtime::runtime_restart(state.inner())
}

#[tauri::command]
fn runtime_logs(
    state: tauri::State<'_, runtime::SharedRuntime>,
    tail: Option<usize>,
) -> runtime::RuntimeLogsResponse {
    runtime::runtime_logs(state.inner(), tail.unwrap_or(200))
}

#[tauri::command]
fn open_diagnostics_folder(
    state: tauri::State<'_, runtime::SharedRuntime>,
) -> runtime::RuntimePathResponse {
    runtime::open_diagnostics_folder(state.inner())
}

#[tauri::command]
fn export_diagnostics(
    state: tauri::State<'_, runtime::SharedRuntime>,
) -> runtime::RuntimePathResponse {
    runtime::export_diagnostics(state.inner())
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .setup(|app| {
            let supervisor = runtime::RuntimeSupervisor::new(app.handle());
            let shared = Arc::new(Mutex::new(supervisor));
            app.manage(shared.clone());
            runtime::spawn_runtime(app.handle().clone(), shared);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            app_info,
            runtime_status,
            runtime_restart,
            runtime_logs,
            open_diagnostics_folder,
            export_diagnostics
        ])
        .run(tauri::generate_context!())
        .expect("error while running Merak desktop app");
}
