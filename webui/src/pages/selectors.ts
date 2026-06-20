type Countable = { id: string };
type StatusItem = Countable & { status?: string };

export type DashboardData = Record<string, unknown>;

export interface WorldMetricInput {
  agents: Countable[] | null;
  chapters: StatusItem[] | null;
  scenes: StatusItem[] | null;
  files: Countable[] | null;
  overview: { agents?: Countable[] } | null;
  dashboard: DashboardData | null;
}

export interface WorldMetrics {
  characterCount: number | null;
  chapterCount: number | null;
  completedChapterCount: number | null;
  sceneCount: number | null;
  completedSceneCount: number | null;
  fileCount: number | null;
  chapterCompletionPercent: number | null;
  sceneCompletionPercent: number | null;
  characterSource: 'dashboard' | 'list' | 'overview' | null;
  chapterSource: 'dashboard' | 'list' | null;
  sceneSource: 'dashboard' | 'list' | null;
  fileSource: 'list' | null;
  chapterProgressSource: 'dashboard' | 'list' | null;
  sceneProgressSource: 'dashboard' | 'list' | null;
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
  const dashboardCharacters = nestedNumber(input.dashboard, 'agents', 'total');
  const dashboardChapters = nestedNumber(input.dashboard, 'chapters', 'total');
  const dashboardCompletedChapters = nestedNumber(input.dashboard, 'chapters', 'completed');
  const dashboardRevisedChapters = nestedNumber(input.dashboard, 'chapters', 'revised');
  const dashboardScenes = nestedNumber(input.dashboard, 'scenes', 'total');
  const dashboardCompletedScenes = nestedNumber(input.dashboard, 'scenes', 'completed');
  const derivedCompletedChapters = input.chapters?.filter((item) =>
    ['completed', 'revised'].includes(item.status ?? ''),
  ).length;
  const derivedCompletedScenes = input.scenes?.filter((item) => item.status === 'completed').length;
  const characterCount =
    dashboardCharacters ?? input.agents?.length ?? input.overview?.agents?.length ?? null;
  const chapterCount = dashboardChapters ?? input.chapters?.length ?? null;
  const completedChapterCount =
    dashboardCompletedChapters !== null
      ? dashboardCompletedChapters + (dashboardRevisedChapters ?? 0)
      : (derivedCompletedChapters ?? null);
  const sceneCount = dashboardScenes ?? input.scenes?.length ?? null;
  const completedSceneCount = dashboardCompletedScenes ?? derivedCompletedScenes ?? null;
  const chapterPercent = nestedNumber(input.dashboard, 'progress', 'chapter_completion_pct');
  const scenePercent = nestedNumber(input.dashboard, 'progress', 'scene_completion_pct');

  return {
    characterCount,
    chapterCount,
    completedChapterCount,
    sceneCount,
    completedSceneCount,
    fileCount: input.files?.length ?? null,
    chapterCompletionPercent:
      chapterPercent ??
      (completedChapterCount !== null && chapterCount !== null
        ? percentage(completedChapterCount, chapterCount)
        : null),
    sceneCompletionPercent:
      scenePercent ??
      (completedSceneCount !== null && sceneCount !== null
        ? percentage(completedSceneCount, sceneCount)
        : null),
    characterSource:
      dashboardCharacters !== null
        ? 'dashboard'
        : input.agents !== null
          ? 'list'
          : input.overview?.agents
            ? 'overview'
            : null,
    chapterSource: dashboardChapters !== null ? 'dashboard' : input.chapters ? 'list' : null,
    sceneSource: dashboardScenes !== null ? 'dashboard' : input.scenes ? 'list' : null,
    fileSource: input.files ? 'list' : null,
    chapterProgressSource:
      chapterPercent !== null || (dashboardChapters !== null && dashboardCompletedChapters !== null)
        ? 'dashboard'
        : input.chapters
          ? 'list'
          : null,
    sceneProgressSource:
      scenePercent !== null || (dashboardScenes !== null && dashboardCompletedScenes !== null)
        ? 'dashboard'
        : input.scenes
          ? 'list'
          : null,
  };
}
