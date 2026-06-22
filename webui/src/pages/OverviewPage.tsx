import {
  BookOpen,
  Clock3,
  Files,
  MessageSquarePlus,
  PanelsTopLeft,
  Settings,
  Users,
} from 'lucide-react';
import { api } from '../api/client';
import type {
  SessionSummary,
  StoryChapter,
  StoryOverview,
  StoryScene,
  WorkspaceFile,
  WorldAgent,
} from '../api/types';
import { worldbuildingApi } from '../api/worldbuilding';
import type { AppPage } from '../AppState';
import PageState from '../components/layout/PageState';
import { useResource } from '../hooks/useResource';
import styles from './OverviewPage.module.css';
import { selectWorldMetrics, type DashboardData } from './selectors';

interface OverviewPageProps {
  worldId: string;
  sessions: SessionSummary[];
  onNavigate(page: AppPage): void;
}

interface OverviewData {
  agents: WorldAgent[] | null;
  chapters: StoryChapter[] | null;
  scenes: StoryScene[] | null;
  files: WorkspaceFile[] | null;
  overview: StoryOverview | null;
  dashboard: DashboardData | null;
  dashboardFailed: boolean;
  failedLists: Array<'agents' | 'chapters' | 'scenes' | 'files'>;
}

async function loadOverview(worldId: string): Promise<OverviewData> {
  const [agents, chapters, scenes, files, overview, dashboard] = await Promise.allSettled([
    api.listAgents(worldId),
    api.listChapters(worldId),
    api.listScenes(worldId),
    api.listWorkspaceFiles({ world_id: worldId }),
    api.getStoryOverview(worldId),
    worldbuildingApi.getDashboard(worldId),
  ]);

  const required = [agents, chapters, scenes, files, overview];
  if (required.every((result) => result.status === 'rejected')) {
    throw new Error('Overview data could not be loaded.');
  }

  return {
    agents: agents.status === 'fulfilled' ? (agents.value.agents ?? []) : null,
    chapters: chapters.status === 'fulfilled' ? (chapters.value.chapters ?? []) : null,
    scenes: scenes.status === 'fulfilled' ? (scenes.value.scenes ?? []) : null,
    files: files.status === 'fulfilled' ? (files.value.files ?? []) : null,
    overview: overview.status === 'fulfilled' ? overview.value.overview : null,
    dashboard:
      dashboard.status === 'fulfilled'
        ? ((dashboard.value.dashboard as DashboardData | undefined) ?? null)
        : null,
    dashboardFailed: dashboard.status === 'rejected',
    failedLists: [
      ...(agents.status === 'rejected' ? (['agents'] as const) : []),
      ...(chapters.status === 'rejected' ? (['chapters'] as const) : []),
      ...(scenes.status === 'rejected' ? (['scenes'] as const) : []),
      ...(files.status === 'rejected' ? (['files'] as const) : []),
    ],
  };
}

function formatUpdatedAt(value: string) {
  const date = new Date(value);
  return Number.isNaN(date.getTime())
    ? value
    : new Intl.DateTimeFormat(undefined, { month: 'short', day: 'numeric' }).format(date);
}

export default function OverviewPage({ worldId, sessions, onNavigate }: OverviewPageProps) {
  const resource = useResource(`overview:${worldId}`, () => loadOverview(worldId));
  const data = resource.data;

  if (!data) {
    return (
      <PageState
        loading={resource.status === 'loading'}
        loadingLabel="Loading overview"
        error={resource.error}
        onRetry={resource.retry}
      />
    );
  }

  const metrics = selectWorldMetrics(data);
  const activeSessions = sessions
    .filter((session) => session.world_id === worldId && !session.archived_at)
    .sort((a, b) => b.updated_at.localeCompare(a.updated_at))
    .slice(0, 3);
  const incompleteChapters =
    data.chapters?.filter((chapter) => !['completed', 'revised'].includes(chapter.status)) ?? null;
  const incompleteScenes = data.scenes?.filter((scene) => scene.status !== 'completed') ?? null;
  const metricValues = [
    metrics.characterCount,
    metrics.chapterCount,
    metrics.sceneCount,
    metrics.fileCount,
  ];
  const isEmpty = metricValues.every((value) => value === 0) && activeSessions.length === 0;

  if (isEmpty) {
    return (
      <div className={styles.page}>
        <section className={styles.empty}>
          <BookOpen size={30} aria-hidden="true" />
          <h1>Your world is ready for its first details</h1>
          <p>Add a character or chapter to begin shaping this world.</p>
          <div className={styles.emptyActions}>
            <button type="button" onClick={() => onNavigate('sessions')}>
              Open sessions
            </button>
            <button type="button" onClick={() => onNavigate('settings')}>
              Settings
            </button>
          </div>
        </section>
      </div>
    );
  }

  const sourceCopy = (source: 'dashboard' | 'list' | 'overview' | null, listLabel: string) =>
    source === 'dashboard'
      ? 'Counted from the worldbuilding dashboard.'
      : source === 'overview'
        ? 'Counted from the story overview.'
        : `Counted from the ${listLabel}.`;
  const metricCards = [
    [
      'Characters',
      metrics.characterCount,
      sourceCopy(metrics.characterSource, 'world character list'),
      Users,
    ],
    ['Chapters', metrics.chapterCount, sourceCopy(metrics.chapterSource, 'chapter list'), BookOpen],
    ['Scenes', metrics.sceneCount, sourceCopy(metrics.sceneSource, 'scene list'), PanelsTopLeft],
    ['Files', metrics.fileCount, sourceCopy(metrics.fileSource, 'workspace file list'), Files],
  ].filter((card): card is [string, number, string, typeof Users] => card[1] !== null);

  return (
    <main className={styles.page}>
      {(data.dashboardFailed || data.failedLists.length > 0) && (
        <div className={styles.notice} role="status">
          {data.dashboardFailed && 'Dashboard summary is unavailable. '}
          {data.failedLists.length > 0
            ? 'Some overview data is unavailable. Available sections use verified sources.'
            : 'Showing derived resource counts.'}
        </div>
      )}

      <section className={styles.metrics} aria-label="World metrics">
        {metricCards.map(([label, value, source, Icon]) => (
          <article className={styles.metricCard} key={label} title={source}>
            <div>
              <span>{label}</span>
              <strong>{value}</strong>
              <small>{source}</small>
            </div>
            <Icon size={24} aria-hidden="true" />
          </article>
        ))}
      </section>

      <div className={styles.grid}>
        <section className={styles.panel}>
          <header>
            <h2>Recent sessions</h2>
            <button onClick={() => onNavigate('sessions')}>View all</button>
          </header>
          {activeSessions.length ? (
            <div className={styles.sessionList}>
              {activeSessions.map((session) => (
                <article className={styles.session} key={session.id}>
                  <MessageSquarePlus size={18} aria-hidden="true" />
                  <div>
                    <strong>{session.title || 'Untitled session'}</strong>
                    <span>Updated {formatUpdatedAt(session.updated_at)}</span>
                  </div>
                </article>
              ))}
            </div>
          ) : (
            <p className={styles.muted}>No sessions are linked to this world yet.</p>
          )}
        </section>

        <section className={styles.panel}>
          <header>
            <h2>Current progress</h2>
          </header>
          {metrics.chapterCompletionPercent !== null &&
            metrics.completedChapterCount !== null &&
            metrics.chapterCount !== null && (
              <ProgressRow
                label="Chapter completion"
                value={metrics.chapterCompletionPercent}
                detail={`${metrics.completedChapterCount} of ${metrics.chapterCount} chapters`}
                source={
                  metrics.chapterProgressSource === 'dashboard'
                    ? 'Calculated from the worldbuilding dashboard.'
                    : 'Calculated from chapter statuses in the chapter list.'
                }
              />
            )}
          {metrics.sceneCompletionPercent !== null &&
            metrics.completedSceneCount !== null &&
            metrics.sceneCount !== null && (
              <ProgressRow
                label="Scene completion"
                value={metrics.sceneCompletionPercent}
                detail={`${metrics.completedSceneCount} of ${metrics.sceneCount} scenes`}
                source={
                  metrics.sceneProgressSource === 'dashboard'
                    ? 'Calculated from the worldbuilding dashboard.'
                    : 'Calculated from scene statuses in the scene list.'
                }
              />
            )}
          {metrics.chapterCompletionPercent === null && metrics.sceneCompletionPercent === null && (
            <p className={styles.muted}>Progress sources are unavailable.</p>
          )}
        </section>

        <section className={styles.panel}>
          <header>
            <h2>Reminders</h2>
          </header>
          {incompleteChapters?.slice(0, 3).map((chapter) => (
            <button
              className={styles.reminder}
              key={chapter.id}
              onClick={() => onNavigate('chapters')}
            >
              <Clock3 size={17} aria-hidden="true" />
              <span>
                <strong>{chapter.title}</strong>
                <small>Chapter status: {chapter.status}</small>
              </span>
            </button>
          ))}
          {incompleteScenes?.slice(0, 3).map((scene) => (
            <button className={styles.reminder} key={scene.id} onClick={() => onNavigate('scenes')}>
              <Clock3 size={17} aria-hidden="true" />
              <span>
                <strong>{scene.title}</strong>
                <small>Scene status: {scene.status}</small>
              </span>
            </button>
          ))}
          {incompleteChapters === null && incompleteScenes === null && (
            <p className={styles.muted}>Reminder sources are unavailable.</p>
          )}
          {(incompleteChapters === null) !== (incompleteScenes === null) && (
            <p className={styles.muted}>Some reminder sources are unavailable.</p>
          )}
          {incompleteChapters !== null &&
            incompleteScenes !== null &&
            !incompleteChapters.length &&
            !incompleteScenes.length && (
              <p className={styles.muted}>No incomplete chapters or scenes.</p>
            )}
        </section>

        <section className={styles.panel}>
          <header>
            <h2>Quick links</h2>
          </header>
          <div className={styles.quickLinks}>
            <button onClick={() => onNavigate('sessions')}>
              <MessageSquarePlus aria-hidden="true" />
              Sessions
            </button>
            <button onClick={() => onNavigate('settings')}>
              <Settings aria-hidden="true" />
              Settings
            </button>
          </div>
        </section>
      </div>
    </main>
  );
}

function ProgressRow({
  label,
  value,
  detail,
  source,
}: {
  label: string;
  value: number;
  detail: string;
  source: string;
}) {
  const boundedValue = Math.max(0, Math.min(100, Math.round(value)));
  return (
    <div className={styles.progressRow}>
      <div>
        <strong>{label}</strong>
        <span>{boundedValue}%</span>
      </div>
      <div
        className={styles.track}
        role="progressbar"
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={boundedValue}
        title={source}
      >
        <span style={{ width: `${boundedValue}%` }} />
      </div>
      <small>{detail}</small>
    </div>
  );
}
