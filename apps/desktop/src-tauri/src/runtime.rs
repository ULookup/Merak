use serde::Serialize;
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    net::{SocketAddr, TcpListener, TcpStream},
    path::PathBuf,
    process::{Child, Command, Stdio},
    sync::{Arc, Mutex},
    thread,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tauri::{AppHandle, Emitter, Manager};

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RuntimeStatus {
    pub phase: String,
    pub api_base_url: Option<String>,
    pub port: Option<u16>,
    pub pid: Option<u32>,
    pub version: String,
    pub pg_status: String,
    pub config_path: String,
    pub log_path: String,
    pub error: Option<String>,
}

#[derive(Serialize)]
pub struct RuntimeRestartResponse {
    pub ok: bool,
    pub status: RuntimeStatus,
}

#[derive(Serialize)]
pub struct RuntimeLogsResponse {
    pub lines: Vec<String>,
}

#[derive(Serialize)]
pub struct RuntimePathResponse {
    pub ok: bool,
    pub path: String,
}

#[derive(Clone)]
struct RuntimePaths {
    merak_exe: PathBuf,
    app_data: PathBuf,
    merak_home: PathBuf,
    log_dir: PathBuf,
    log_file: PathBuf,
    config_file: PathBuf,
    resource_dir: PathBuf,
}

pub struct RuntimeSupervisor {
    paths: RuntimePaths,
    child: Option<Child>,
    status: RuntimeStatus,
}

impl RuntimeSupervisor {
    pub fn new(app: &AppHandle) -> Self {
        let paths = runtime_paths(app);
        let status = RuntimeStatus {
            phase: "stopped".to_string(),
            api_base_url: None,
            port: None,
            pid: None,
            version: env!("CARGO_PKG_VERSION").to_string(),
            pg_status: "unknown".to_string(),
            config_path: paths.config_file.display().to_string(),
            log_path: paths.log_file.display().to_string(),
            error: None,
        };
        Self {
            paths,
            child: None,
            status,
        }
    }

    pub fn ensure_started(&mut self) {
        if self.is_child_alive() && self.status.phase == "ready" {
            return;
        }
        if self.is_child_alive() {
            return;
        }
        self.start();
    }

    pub fn restart(&mut self) -> RuntimeStatus {
        self.stop();
        self.start();
        self.status.clone()
    }

    pub fn stop(&mut self) {
        if let Some(mut child) = self.child.take() {
            let _ = child.kill();
            let _ = child.wait();
        }
        self.status.phase = "stopped".to_string();
        self.status.pid = None;
    }

    pub fn status(&mut self) -> RuntimeStatus {
        if let Some(child) = self.child.as_mut() {
            match child.try_wait() {
                Ok(Some(exit)) => {
                    self.status.phase = "failed".to_string();
                    self.status.error = Some(format!("Merak runtime exited with {exit}"));
                    self.status.pid = None;
                    self.child = None;
                }
                Ok(None) => {}
                Err(error) => {
                    self.status.phase = "failed".to_string();
                    self.status.error = Some(error.to_string());
                }
            }
        }
        self.status.clone()
    }

    pub fn logs(&self, tail: usize) -> Vec<String> {
        let content = fs::read_to_string(&self.paths.log_file).unwrap_or_default();
        let lines: Vec<String> = content.lines().map(str::to_string).collect();
        if tail == 0 || lines.len() <= tail {
            return lines;
        }
        lines[lines.len() - tail..].to_vec()
    }

    pub fn diagnostics_dir(&self) -> PathBuf {
        self.paths.log_dir.clone()
    }

    pub fn export_diagnostics(&self) -> std::io::Result<PathBuf> {
        fs::create_dir_all(&self.paths.log_dir)?;
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        let path = self.paths.log_dir.join(format!("merak-diagnostics-{stamp}.txt"));
        let mut out = File::create(&path)?;
        writeln!(out, "Merak Desktop Diagnostics")?;
        writeln!(out, "version={}", self.status.version)?;
        writeln!(out, "phase={}", self.status.phase)?;
        writeln!(out, "api_base_url={:?}", self.status.api_base_url)?;
        writeln!(out, "port={:?}", self.status.port)?;
        writeln!(out, "pid={:?}", self.status.pid)?;
        writeln!(out, "pg_status={}", self.status.pg_status)?;
        writeln!(out, "config_path={}", self.status.config_path)?;
        writeln!(out, "log_path={}", self.status.log_path)?;
        writeln!(out, "error={:?}", self.status.error)?;
        writeln!(out, "\n--- Runtime Log ---")?;
        for line in self.logs(400) {
            writeln!(out, "{line}")?;
        }
        Ok(path)
    }

    fn start(&mut self) {
        self.status.phase = "starting".to_string();
        self.status.error = None;
        self.status.pg_status = if self.paths.resource_dir.join("pg").exists() {
            "bundled".to_string()
        } else {
            "external-or-unavailable".to_string()
        };

        if let Err(error) = self.prepare_files() {
            self.status.phase = "failed".to_string();
            self.status.error = Some(error.to_string());
            return;
        }

        if !self.paths.merak_exe.exists() {
            self.status.phase = "failed".to_string();
            self.status.error = Some(format!(
                "Merak runtime binary was not found at {}",
                self.paths.merak_exe.display()
            ));
            return;
        }

        let Some(port) = find_free_port() else {
            self.status.phase = "failed".to_string();
            self.status.error = Some("No free local port found for Merak runtime".to_string());
            return;
        };

        self.status.port = Some(port);
        self.status.api_base_url = Some(format!("http://127.0.0.1:{port}"));

        let log = match OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.paths.log_file)
        {
            Ok(file) => file,
            Err(error) => {
                self.status.phase = "failed".to_string();
                self.status.error = Some(error.to_string());
                return;
            }
        };

        let stderr = match log.try_clone() {
            Ok(file) => file,
            Err(error) => {
                self.status.phase = "failed".to_string();
                self.status.error = Some(error.to_string());
                return;
            }
        };

        let mut command = Command::new(&self.paths.merak_exe);
        command
            .arg("serve")
            .arg("--port")
            .arg(port.to_string())
            .current_dir(&self.paths.app_data)
            .env("MERAK_HOME", &self.paths.merak_home)
            .env("MERAK_DESKTOP", "1")
            .env("MERAK_RESOURCE_DIR", &self.paths.resource_dir)
            .stdout(Stdio::from(log))
            .stderr(Stdio::from(stderr));

        match command.spawn() {
            Ok(child) => {
                self.status.pid = Some(child.id());
                self.child = Some(child);
            }
            Err(error) => {
                self.status.phase = "failed".to_string();
                self.status.error = Some(error.to_string());
                return;
            }
        }

        if wait_for_runtime(port, Duration::from_secs(25)) {
            self.status.phase = "ready".to_string();
        } else {
            self.status.phase = "failed".to_string();
            self.status.error = Some("Merak runtime did not become ready in time".to_string());
        }
    }

    fn prepare_files(&self) -> std::io::Result<()> {
        fs::create_dir_all(&self.paths.app_data)?;
        fs::create_dir_all(&self.paths.merak_home)?;
        fs::create_dir_all(&self.paths.log_dir)?;
        if !self.paths.config_file.exists() {
            fs::write(&self.paths.config_file, desktop_config_template())?;
        }
        Ok(())
    }

    fn is_child_alive(&mut self) -> bool {
        match self.child.as_mut().map(Child::try_wait) {
            Some(Ok(None)) => true,
            Some(Ok(Some(_))) | Some(Err(_)) => {
                self.child = None;
                false
            }
            None => false,
        }
    }
}

impl Drop for RuntimeSupervisor {
    fn drop(&mut self) {
        self.stop();
    }
}

pub type SharedRuntime = Arc<Mutex<RuntimeSupervisor>>;

pub fn spawn_runtime(app: AppHandle, runtime: SharedRuntime) {
    thread::spawn(move || {
        if let Ok(mut supervisor) = runtime.lock() {
            supervisor.ensure_started();
        }
        app.emit("merak://runtime-status", ()).ok();
    });
}

pub fn runtime_status(runtime: &SharedRuntime) -> RuntimeStatus {
    let mut supervisor = runtime.lock().expect("runtime lock poisoned");
    supervisor.status()
}

pub fn runtime_restart(runtime: &SharedRuntime) -> RuntimeRestartResponse {
    let mut supervisor = runtime.lock().expect("runtime lock poisoned");
    let status = supervisor.restart();
    RuntimeRestartResponse { ok: status.phase == "ready", status }
}

pub fn runtime_logs(runtime: &SharedRuntime, tail: usize) -> RuntimeLogsResponse {
    let supervisor = runtime.lock().expect("runtime lock poisoned");
    RuntimeLogsResponse {
        lines: supervisor.logs(tail),
    }
}

pub fn open_diagnostics_folder(runtime: &SharedRuntime) -> RuntimePathResponse {
    let supervisor = runtime.lock().expect("runtime lock poisoned");
    let path = supervisor.diagnostics_dir();
    let path_string = path.display().to_string();
    #[cfg(target_os = "windows")]
    {
        let _ = Command::new("explorer").arg(&path).spawn();
    }
    RuntimePathResponse {
        ok: true,
        path: path_string,
    }
}

pub fn export_diagnostics(runtime: &SharedRuntime) -> RuntimePathResponse {
    let supervisor = runtime.lock().expect("runtime lock poisoned");
    match supervisor.export_diagnostics() {
        Ok(path) => RuntimePathResponse {
            ok: true,
            path: path.display().to_string(),
        },
        Err(error) => RuntimePathResponse {
            ok: false,
            path: error.to_string(),
        },
    }
}

fn runtime_paths(app: &AppHandle) -> RuntimePaths {
    let app_data = app
        .path()
        .app_data_dir()
        .unwrap_or_else(|_| std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")));
    let merak_home = app_data.clone();
    let log_dir = app_data.join("logs");
    let log_file = log_dir.join("merak-runtime.log");
    let config_file = merak_home.join("settings.local.json");
    let resource_dir = app
        .path()
        .resource_dir()
        .unwrap_or_else(|_| dev_repo_root().unwrap_or_else(|| app_data.clone()));
    let merak_exe = find_merak_exe(&resource_dir).unwrap_or_else(|| resource_dir.join("merak.exe"));
    RuntimePaths {
        merak_exe,
        app_data,
        merak_home,
        log_dir,
        log_file,
        config_file,
        resource_dir,
    }
}

fn find_merak_exe(resource_dir: &PathBuf) -> Option<PathBuf> {
    let candidates = [
        resource_dir.join("merak.exe"),
        resource_dir.join("bin").join("merak.exe"),
        resource_dir.join("resources").join("merak.exe"),
    ];
    for candidate in candidates {
        if candidate.exists() {
            return Some(candidate);
        }
    }
    let root = dev_repo_root()?;
    [
        root.join("build").join("cli").join("Debug").join("merak.exe"),
        root.join("build").join("cli").join("Release").join("merak.exe"),
        root.join("build").join("cli").join("merak.exe"),
    ]
    .into_iter()
    .find(|candidate| candidate.exists())
}

fn dev_repo_root() -> Option<PathBuf> {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest_dir.parent()?.parent()?.parent().map(PathBuf::from)
}

fn find_free_port() -> Option<u16> {
    let listener = TcpListener::bind(("127.0.0.1", 0)).ok()?;
    let addr = listener.local_addr().ok()?;
    Some(addr.port())
}

fn wait_for_runtime(port: u16, timeout: Duration) -> bool {
    let start = std::time::Instant::now();
    while start.elapsed() < timeout {
        if runtime_ready(port) {
            return true;
        }
        thread::sleep(Duration::from_millis(500));
    }
    false
}

fn runtime_ready(port: u16) -> bool {
    let addr = SocketAddr::from(([127, 0, 0, 1], port));
    let Ok(mut stream) = TcpStream::connect_timeout(&addr, Duration::from_millis(500)) else {
        return false;
    };
    stream
        .set_read_timeout(Some(Duration::from_millis(700)))
        .ok();
    let request = b"GET /v1/runtime HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if stream.write_all(request).is_err() {
        return false;
    }
    let mut response = String::new();
    if stream.read_to_string(&mut response).is_err() {
        return false;
    }
    response.starts_with("HTTP/1.1 200") || response.starts_with("HTTP/1.0 200")
}

fn desktop_config_template() -> &'static str {
    r#"{
  "llm": {
    "provider": "openai",
    "api_key": "sk-your-api-key-here",
    "api_base_url": "https://api.openai.com/v1",
    "default_model": "gpt-4o",
    "max_output_tokens": 4096
  },
  "agent": {
    "system_prompt": "You are Merak, a narrative AI workbench assistant. Help the author build worlds, characters, scenes, and long-form story context.",
    "permission_mode": "ask"
  },
  "memory": {
    "enabled": true
  },
  "knowledge_graph": {
    "enabled": false
  }
}
"#
}
