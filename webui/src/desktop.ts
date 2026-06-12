import { invoke } from '@tauri-apps/api/core';

export interface DesktopAppInfo {
  name: string;
  version: string;
  platform: string;
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
