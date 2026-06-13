import { invoke } from '@tauri-apps/api/core';

export interface DesktopAppInfo {
  name: string;
  version: string;
  platform: string;
}

export interface DesktopRuntimeStatus {
  phase: 'stopped' | 'starting' | 'ready' | 'failed' | string;
  apiBaseUrl: string | null;
  port: number | null;
  pid: number | null;
  version: string;
  pgStatus: string;
  configPath: string;
  logPath: string;
  error: string | null;
}

export interface DesktopRuntimeRestartResponse {
  ok: boolean;
  status: DesktopRuntimeStatus;
}

export interface DesktopRuntimeLogsResponse {
  lines: string[];
}

export interface DesktopPathResponse {
  ok: boolean;
  path: string;
}

export function isDesktopApp() {
  return typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;
}

export async function getDesktopAppInfo(): Promise<DesktopAppInfo | null> {
  if (!isDesktopApp()) return null;
  try {
    return await invoke<DesktopAppInfo>('app_info');
  } catch {
    return null;
  }
}

export async function getDesktopRuntimeStatus(): Promise<DesktopRuntimeStatus | null> {
  if (!isDesktopApp()) return null;
  try {
    return await invoke<DesktopRuntimeStatus>('runtime_status');
  } catch {
    return null;
  }
}

export async function restartDesktopRuntime(): Promise<DesktopRuntimeRestartResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return await invoke<DesktopRuntimeRestartResponse>('runtime_restart');
  } catch {
    return null;
  }
}

export async function getDesktopRuntimeLogs(tail = 200): Promise<DesktopRuntimeLogsResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return await invoke<DesktopRuntimeLogsResponse>('runtime_logs', { tail });
  } catch {
    return null;
  }
}

export async function openDiagnosticsFolder(): Promise<DesktopPathResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return await invoke<DesktopPathResponse>('open_diagnostics_folder');
  } catch {
    return null;
  }
}

export async function exportDiagnostics(): Promise<DesktopPathResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return await invoke<DesktopPathResponse>('export_diagnostics');
  } catch {
    return null;
  }
}
