import { lazy, Suspense, useEffect, useState, type ReactNode } from 'react';
import { ArrowLeft } from 'lucide-react';
import { api } from './api/client';
import styles from './App.module.css';
import { AppStateProvider, useAppState, type AppState } from './AppState';
import AskUserPrompt from './components/AskUserPrompt';
import ChapterEditor from './components/ChapterEditor';
import ConnectionBanner from './components/ConnectionBanner';
import CreationRequestDialog from './components/CreationRequestDialog';
import ErrorBoundary from './components/ErrorBoundary';
import InspectorPanel from './components/InspectorPanel';
import MainPanel from './components/MainPanel';
import SettingsPage from './components/SettingsPage';
import Skeleton from './components/Skeleton';
import { ToastProvider } from './components/Toast';
import WorldDashboard from './components/WorldDashboard';
import WorldOnboarding from './components/WorldOnboarding';
import WorldSidebar from './components/WorldSidebar';
import DesktopBoot from './DesktopBoot';
import { useSSE } from './hooks/useSSE';
import { I18nProvider } from './i18n';
import DesktopShell from './shell/DesktopShell';

const HelpDrawer = lazy(() => import('./components/HelpDrawer'));
const SetupWizard = lazy(() => import('./components/SetupWizard'));
const ChapterReviewBanner = lazy(() => import('./components/ChapterReviewBanner'));
const ExportDialog = lazy(() => import('./components/ExportDialog'));
const OverviewPage = lazy(() => import('./pages/OverviewPage'));
const SessionsPage = lazy(() => import('./pages/SessionsPage'));

export function shouldReportWorldbuildingPartialFailure(
  overviewLoaded: boolean,
  hasSecondaryFailure: boolean,
) {
  return !overviewLoaded && hasSecondaryFailure;
}

export function shouldWarnBeforeClose(state: AppState) {
  const runActive = Boolean(state.currentRun) && state.status !== 'idle';
  const editorUnsafe = state.editorSaveStatus === 'dirty' || state.editorSaveStatus === 'saving';
  return runActive || editorUnsafe;
}

function AppInner() {
  const { state, dispatch } = useAppState();
  const [sidebarOpen, setSidebarOpen] = useState(window.innerWidth >= 768);
  const [inspectorOpen, setInspectorOpen] = useState(window.innerWidth >= 1180);
  const [helpOpen, setHelpOpen] = useState(false);
  const [bootstrapped, setBootstrapped] = useState(false);

  // Bootstrap: load metadata, worlds, sessions, capabilities once
  useEffect(() => {
    async function bootstrap() {
      // Check if LLM is configured — if not, show setup wizard
      try {
        const config = await api.getConfig();
        if (!config.api_key_masked) {
          dispatch({ type: 'SHOW_SETUP_WIZARD', show: true });
        }
      } catch {
        dispatch({ type: 'SHOW_SETUP_WIZARD', show: true });
      }

      const [metadataRes, worldsRes, sessionsRes, capabilitiesRes, prefsRes] =
        await Promise.allSettled([
          api.metadata(),
          api.listWorlds(),
          api.listSessions(),
          api.capabilities(),
          api.getPreferences(),
        ]);

      if (metadataRes.status === 'fulfilled') {
        dispatch({ type: 'SET_METADATA', metadata: metadataRes.value });
      }
      if (worldsRes.status === 'fulfilled') {
        dispatch({ type: 'SET_WORLDS', worlds: worldsRes.value.worlds ?? [] });
      }
      if (sessionsRes.status === 'fulfilled') {
        dispatch({ type: 'SET_SESSIONS', sessions: sessionsRes.value.sessions ?? [] });
      }
      if (capabilitiesRes.status === 'fulfilled') {
        dispatch({
          type: 'SET_CAPABILITIES',
          capabilities: capabilitiesRes.value.capabilities,
          fallback: capabilitiesRes.value.fallback,
        });
      }
      if (prefsRes.status === 'fulfilled' && prefsRes.value) {
        dispatch({
          type: 'SET_USER_PREFERENCES',
          prefs: {
            default_genre: prefsRes.value.default_genre ?? '',
            preferred_style: prefsRes.value.preferred_style ?? '轻松',
          },
        });
      }

      const worlds = worldsRes.status === 'fulfilled' ? (worldsRes.value.worlds ?? []) : [];
      const sessions = sessionsRes.status === 'fulfilled' ? (sessionsRes.value.sessions ?? []) : [];

      if (worlds.length === 0) {
        dispatch({ type: 'SET_APP_PHASE', phase: 'no_world' });
        setBootstrapped(true);
        return;
      }

      // Try to restore last active session with a world binding
      const activeSession = sessions.find((s) => !s.archived_at && s.world_id);

      if (activeSession) {
        dispatch({ type: 'SET_WORLD', worldId: activeSession.world_id });
        dispatch({
          type: 'SET_AGENT_SESSION',
          sessionId: activeSession.id,
          agentId: activeSession.agent_id ?? '',
        });
      } else {
        dispatch({ type: 'SET_WORLD', worldId: worlds[0].id });
      }

      setBootstrapped(true);
    }

    bootstrap();
  }, [dispatch]);

  // Worldbuilding data loading when worldId changes
  useEffect(() => {
    if (!state.worldId) return;
    let cancelled = false;
    dispatch({ type: 'SET_WORLDBUILDING_STATUS', status: 'loading' });

    Promise.allSettled([
      api.getStoryOverview(state.worldId, state.sessionId),
      api.listWorlds(),
      api.listAgents(state.worldId),
      api.listForeshadowing(state.worldId),
      api.listSecrets(state.worldId),
      api.getWorldTime(state.worldId),
    ]).then(([overviewRes, worldsRes, agentsRes, foreshadowingRes, secretsRes, timeRes]) => {
      if (cancelled) return;
      const worlds =
        worldsRes.status === 'fulfilled' ? (worldsRes.value.worlds ?? state.worlds) : state.worlds;
      const overview = overviewRes.status === 'fulfilled' ? overviewRes.value.overview : null;
      const agents = agentsRes.status === 'fulfilled' ? (agentsRes.value.agents ?? []) : [];
      const foreshadowing =
        overview?.foreshadowing ??
        (foreshadowingRes.status === 'fulfilled'
          ? (foreshadowingRes.value.foreshadowing ?? foreshadowingRes.value.items ?? [])
          : []);
      const secrets =
        overview?.secrets ??
        (secretsRes.status === 'fulfilled'
          ? (secretsRes.value.secrets ?? secretsRes.value.items ?? [])
          : []);
      const worldTime =
        overview?.world_time ??
        (timeRes.status === 'fulfilled'
          ? ((timeRes.value.label ??
              timeRes.value.time ??
              timeRes.value.world_time ??
              timeRes.value.now ??
              null) as string | null)
          : null);

      const secondaryFailed = [agentsRes, foreshadowingRes, secretsRes, timeRes].some(
        (r) => r.status === 'rejected',
      );
      dispatch({
        type: 'SET_WORLDBUILDING_DATA',
        worlds,
        agents: overview?.agents?.length ? overview.agents : agents,
        foreshadowing,
        secrets,
        worldTime,
        storyOverview: overview,
        fallback: overviewRes.status === 'fulfilled' ? overviewRes.value.fallback : false,
      });
      if (
        shouldReportWorldbuildingPartialFailure(overviewRes.status === 'fulfilled', secondaryFailed)
      ) {
        dispatch({
          type: 'SET_WORLDBUILDING_STATUS',
          status: 'error',
          error: 'Some story context could not be loaded.',
        });
      }
    });

    return () => {
      cancelled = true;
    };
  }, [state.worldId, state.sessionId, state.storyVersion, dispatch]);

  // Esc key cancels current run
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        if (state.currentRun && state.status !== 'idle') {
          api.cancelRun(state.currentRun).catch(() => {});
        }
        return;
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [state.currentRun, state.status]);

  useEffect(() => {
    const handleBeforeUnload = (event: BeforeUnloadEvent) => {
      if (!shouldWarnBeforeClose(state)) return;
      event.preventDefault();
      event.returnValue = '';
    };
    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => window.removeEventListener('beforeunload', handleBeforeUnload);
  }, [state]);

  // SSE connection: only in 'ready' phase
  const sseUrl = state.appPhase === 'ready' && state.sessionId ? api.sseUrl(state.sessionId) : null;

  const connState = useSSE(sseUrl, dispatch, state.lastSeq);

  const inDesktopShell = (children: ReactNode, overlays?: ReactNode) => (
    <DesktopShell
      page={state.currentPage}
      onNavigate={(page) => dispatch({ type: 'SET_PAGE', page })}
      overlays={overlays}
    >
      {children}
    </DesktopShell>
  );

  // Page-based rendering (overrides phase when navigating away from workbench)
  if (state.currentPage === 'settings') {
    return inDesktopShell(<SettingsPage />);
  }

  if (state.currentPage === 'editor' && state.activeEditorChapterId && state.worldId) {
    return (
      <div className={styles.editorOverlay}>
        <div className={styles.editorHeader}>
          <button
            className={styles.editorBackBtn}
            onClick={() => dispatch({ type: 'SET_PAGE', page: 'overview' })}
          >
            <ArrowLeft size={15} aria-hidden="true" strokeWidth={2.3} />
            返回工作台
          </button>
          <h1 className={styles.editorTitle}>{state.activeEditorChapterTitle || '章节编辑'}</h1>
        </div>
        <div className={styles.editorBody}>
          <ChapterEditor chapterId={state.activeEditorChapterId} worldId={state.worldId} />
        </div>
      </div>
    );
  }

  // Phase-based rendering (workbench page)
  if (!bootstrapped || state.appPhase === 'loading') {
    return inDesktopShell(<Skeleton />);
  }

  if (state.appPhase === 'no_world') {
    return (
      <ToastProvider>
        {inDesktopShell(
          <WorldOnboarding onOpenGuide={() => setHelpOpen(true)} />,
          <Suspense>
            {helpOpen && <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />}
          </Suspense>,
        )}
      </ToastProvider>
    );
  }

  if (state.currentPage === 'overview' && state.worldId) {
    return inDesktopShell(
      <Suspense fallback={<Skeleton />}>
        <OverviewPage
          worldId={state.worldId}
          sessions={state.sessions}
          onNavigate={(page) => dispatch({ type: 'SET_PAGE', page })}
        />
      </Suspense>,
    );
  }

  if (state.appPhase === 'no_agent') {
    return (
      <ToastProvider>
        {inDesktopShell(
          <WorldDashboard onOpenGuide={() => setHelpOpen(true)} />,
          <Suspense>
            {helpOpen && <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />}
          </Suspense>,
        )}
      </ToastProvider>
    );
  }

  // appPhase === 'ready': three-column Workbench
  return (
    <ToastProvider>
      {inDesktopShell(
        state.currentPage === 'sessions' ? (
          <Suspense fallback={<Skeleton />}>
            <SessionsPage connectionState={connState} onOpenGuide={() => setHelpOpen(true)} />
          </Suspense>
        ) : (
          <div className={styles.layout}>
            <ErrorBoundary>
              <WorldSidebar open={sidebarOpen} onClose={() => setSidebarOpen(false)} />
            </ErrorBoundary>
            <ErrorBoundary>
              <div className={styles.workspace}>
                <ConnectionBanner state={connState} />
                <MainPanel
                  onToggleSidebar={() => setSidebarOpen((prev) => !prev)}
                  onToggleInspector={() => setInspectorOpen((prev) => !prev)}
                  onOpenGuide={() => setHelpOpen(true)}
                  sidebarOpen={sidebarOpen}
                  inspectorOpen={inspectorOpen}
                  connectionState={connState}
                />
              </div>
            </ErrorBoundary>
            <ErrorBoundary>
              <InspectorPanel open={inspectorOpen} onClose={() => setInspectorOpen(false)} />
            </ErrorBoundary>
          </div>
        ),
        <>
          <Suspense>
            <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />
          </Suspense>

          {/* Setup Wizard — shown when LLM is not configured */}
          <Suspense>
            {state.showSetupWizard && (
              <SetupWizard
                onComplete={() => dispatch({ type: 'SET_LLM_CONFIGURED', configured: true })}
              />
            )}
          </Suspense>

          {/* Chapter Review Banner — shown after chapter completion */}
          <Suspense>
            {state.chapterReview && state.worldId && (
              <ChapterReviewBanner
                worldId={state.worldId}
                chapterId={state.chapterReview.chapter_id}
                chapterTitle={state.chapterReview.title}
                onNewChapter={() => {
                  dispatch({ type: 'SET_CHAPTER_REVIEW', review: null });
                  if (state.sessionId) {
                    api
                      .startRun(state.sessionId, '开始写下一章', state.selectedModel)
                      .catch(() => {});
                  }
                }}
                onRevise={() => {
                  dispatch({ type: 'SET_CHAPTER_REVIEW', review: null });
                }}
                onExport={() => dispatch({ type: 'SET_SHOW_EXPORT_DIALOG', show: true })}
                onClose={() => dispatch({ type: 'SET_CHAPTER_REVIEW', review: null })}
              />
            )}
          </Suspense>

          {state.pendingAsk && (
            <AskUserPrompt
              request={state.pendingAsk}
              onResolved={(callId) => dispatch({ type: 'RESOLVE_ASK', callId })}
            />
          )}
          {state.pendingCreation && (
            <CreationRequestDialog
              request={state.pendingCreation}
              onResolved={(creationId) => dispatch({ type: 'RESOLVE_CREATION', creationId })}
            />
          )}

          {/* Export Dialog */}
          <Suspense>
            {state.showExportDialog && state.worldId && (
              <ExportDialog
                worldId={state.worldId}
                chapters={[] /* TODO: populate from full chapter list when available in state */}
                onClose={() => dispatch({ type: 'SET_SHOW_EXPORT_DIALOG', show: false })}
              />
            )}
          </Suspense>
        </>,
      )}
    </ToastProvider>
  );
}

export default function App() {
  return (
    <DesktopBoot>
      <I18nProvider>
        <AppStateProvider>
          <AppInner />
        </AppStateProvider>
      </I18nProvider>
    </DesktopBoot>
  );
}
