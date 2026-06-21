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
  error?: string;
}

export interface DesktopRuntimeLogsResponse {
  lines: string[];
  error?: string;
}

export interface DesktopPathResponse {
  ok: boolean;
  path: string;
  error?: string;
}

export function isDesktopApp() {
  return typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;
}

export async function getDesktopAppInfo(): Promise<DesktopAppInfo | null> {
  if (!isDesktopApp()) return null;
  try {
    return (await invoke<DesktopAppInfo>('app_info')) ?? null;
  } catch {
    return null;
  }
}

export async function getDesktopRuntimeStatus(): Promise<DesktopRuntimeStatus | null> {
  if (!isDesktopApp()) return null;
  try {
    return (await invoke<DesktopRuntimeStatus>('runtime_status')) ?? null;
  } catch {
    return null;
  }
}

export async function restartDesktopRuntime(): Promise<DesktopRuntimeRestartResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return (await invoke<DesktopRuntimeRestartResponse>('runtime_restart')) ?? null;
  } catch {
    return null;
  }
}

export async function getDesktopRuntimeLogs(
  tail = 200,
): Promise<DesktopRuntimeLogsResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return (await invoke<DesktopRuntimeLogsResponse>('runtime_logs', { tail })) ?? null;
  } catch {
    return null;
  }
}

export async function openDiagnosticsFolder(): Promise<DesktopPathResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return (await invoke<DesktopPathResponse>('open_diagnostics_folder')) ?? null;
  } catch {
    return null;
  }
}

export async function exportDiagnostics(): Promise<DesktopPathResponse | null> {
  if (!isDesktopApp()) return null;
  try {
    return (await invoke<DesktopPathResponse>('export_diagnostics')) ?? null;
  } catch {
    return null;
  }
}
