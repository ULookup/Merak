type Countable = { id: string };
type StatusItem = Countable & { status?: string };

export type DashboardData = Record<string, unknown>;

export interface WorldMetricInput {
  agents: Countable[];
  chapters: StatusItem[];
  scenes: StatusItem[];
  files: Countable[];
  overview: { agents?: Countable[] } | null;
  dashboard: DashboardData | null;
}

export interface WorldMetrics {
  characterCount: number;
  chapterCount: number;
  completedChapterCount: number;
  sceneCount: number;
  completedSceneCount: number;
  fileCount: number;
  chapterCompletionPercent: number;
  sceneCompletionPercent: number;
}

function nestedNumber(source: DashboardData | null, group: string, key: string) {
  const value = source?.[group];
  if (!value || typeof value !== 'object') return null;
  const candidate = (value as Record<string, unknown>)[key];
  return typeof candidate === 'number' && Number.isFinite(candidate) ? candidate : null;
}

function percentage(completed: number, total: number) {
  return total === 0 ? 0 : Math.round((completed / total) * 100);
}

export function selectWorldMetrics(input: WorldMetricInput): WorldMetrics {
  const derivedAgents = input.agents.length || input.overview?.agents?.length || 0;
  const derivedCompletedChapters = input.chapters.filter((item) =>
    ['completed', 'revised'].includes(item.status ?? ''),
  ).length;
  const derivedCompletedScenes = input.scenes.filter((item) => item.status === 'completed').length;
  const characterCount = nestedNumber(input.dashboard, 'agents', 'total') ?? derivedAgents;
  const chapterCount = nestedNumber(input.dashboard, 'chapters', 'total') ?? input.chapters.length;
  const completedChapterCount =
    nestedNumber(input.dashboard, 'chapters', 'completed') ?? derivedCompletedChapters;
  const sceneCount = nestedNumber(input.dashboard, 'scenes', 'total') ?? input.scenes.length;
  const completedSceneCount =
    nestedNumber(input.dashboard, 'scenes', 'completed') ?? derivedCompletedScenes;

  return {
    characterCount,
    chapterCount,
    completedChapterCount,
    sceneCount,
    completedSceneCount,
    fileCount: input.files.length,
    chapterCompletionPercent:
      nestedNumber(input.dashboard, 'progress', 'chapter_completion_pct') ??
      percentage(completedChapterCount, chapterCount),
    sceneCompletionPercent:
      nestedNumber(input.dashboard, 'progress', 'scene_completion_pct') ??
      percentage(completedSceneCount, sceneCount),
  };
}
