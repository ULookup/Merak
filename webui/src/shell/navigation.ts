import {
  BookOpen,
  Files,
  Globe2,
  KeyRound,
  LayoutDashboard,
  MessagesSquare,
  PanelsTopLeft,
  Settings,
  Sparkles,
  Users,
} from 'lucide-react';
import type { AppPage } from '../AppState';

export const desktopPages = [
  ['overview', '概览', LayoutDashboard],
  ['sessions', 'Sessions 会话', MessagesSquare],
  ['world', 'World 世界设定', Globe2],
  ['characters', 'Characters 角色', Users],
  ['chapters', 'Chapters 章节', BookOpen],
  ['scenes', 'Scenes 场景', PanelsTopLeft],
  ['foreshadowing', 'Foreshadowing 伏笔线索', Sparkles],
  ['secrets', 'Secrets 秘密', KeyRound],
  ['files', 'Files 资料库', Files],
  ['settings', 'Settings 设置', Settings],
] as const;

export type DesktopPage = (typeof desktopPages)[number][0];

const storageKey = 'merak.desktop.page';
const desktopPageIds = new Set<string>(desktopPages.map(([page]) => page));

export function isDesktopPage(page: unknown): page is DesktopPage {
  return typeof page === 'string' && desktopPageIds.has(page);
}

export function readStoredDesktopPage(): DesktopPage {
  if (typeof window === 'undefined') return 'overview';
  const storedPage = window.localStorage.getItem(storageKey);
  return isDesktopPage(storedPage) ? storedPage : 'overview';
}

export function writeStoredDesktopPage(page: AppPage) {
  if (typeof window !== 'undefined' && isDesktopPage(page)) {
    window.localStorage.setItem(storageKey, page);
  }
}
