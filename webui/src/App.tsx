import { useEffect, useState } from 'react';
import { api } from './api/client';
import styles from './App.module.css';
import { AppStateProvider, useAppState } from './AppState';
import ConnectionBanner from './components/ConnectionBanner';
import ErrorBoundary from './components/ErrorBoundary';
import HelpDrawer from './components/HelpDrawer';
import InspectorPanel from './components/InspectorPanel';
import MainPanel from './components/MainPanel';
import Skeleton from './components/Skeleton';
import { ToastProvider } from './components/Toast';
import WorldDashboard from './components/WorldDashboard';
import WorldOnboarding from './components/WorldOnboarding';
import WorldSidebar from './components/WorldSidebar';
import SetupWizard from './components/SetupWizard';
import ChapterReviewBanner from './components/ChapterReviewBanner';
import ExportDialog from './components/ExportDialog';
import DesktopBoot from './DesktopBoot';
import { useSSE } from './hooks/useSSE';
import { I18nProvider } from './i18n';

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

      const [metadataRes, worldsRes, sessionsRes, capabilitiesRes] = await Promise.allSettled([
        api.metadata(),
        api.listWorlds(),
        api.listSessions(),
        api.capabilities(),
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

      const failed = [agentsRes, foreshadowingRes, secretsRes, timeRes].some(
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
      if (failed) {
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

  // SSE connection: only in 'ready' phase
  const sseUrl = state.appPhase === 'ready' && state.sessionId ? api.sseUrl(state.sessionId) : null;

  const connState = useSSE(sseUrl, dispatch, state.lastSeq);

  // Phase-based rendering
  if (!bootstrapped || state.appPhase === 'loading') {
    return <Skeleton />;
  }

  if (state.appPhase === 'no_world') {
    return (
      <ToastProvider>
        <WorldOnboarding onOpenGuide={() => setHelpOpen(true)} />
        <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />
      </ToastProvider>
    );
  }

  if (state.appPhase === 'no_agent') {
    return (
      <ToastProvider>
        <WorldDashboard onOpenGuide={() => setHelpOpen(true)} />
        <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />
      </ToastProvider>
    );
  }

  // appPhase === 'ready': three-column Workbench
  return (
    <ToastProvider>
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
        <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />
        <ErrorBoundary>
          <InspectorPanel open={inspectorOpen} onClose={() => setInspectorOpen(false)} />
        </ErrorBoundary>
      </div>

      {/* Setup Wizard — shown when LLM is not configured */}
      {state.showSetupWizard && (
        <SetupWizard onComplete={() => dispatch({ type: 'SET_LLM_CONFIGURED', configured: true })} />
      )}

      {/* Chapter Review Banner — shown after chapter completion */}
      {state.chapterReview && state.worldId && (
        <ChapterReviewBanner
          worldId={state.worldId}
          chapterId={state.chapterReview.chapter_id}
          chapterTitle={state.chapterReview.title}
          onNewChapter={() => {
            dispatch({ type: 'SET_CHAPTER_REVIEW', review: null });
            if (state.sessionId) {
              api.startRun(state.sessionId, '开始写下一章', state.selectedModel).catch(() => {});
            }
          }}
          onRevise={() => {
            dispatch({ type: 'SET_CHAPTER_REVIEW', review: null });
          }}
          onExport={() => dispatch({ type: 'SET_SHOW_EXPORT_DIALOG', show: true })}
          onClose={() => dispatch({ type: 'SET_CHAPTER_REVIEW', review: null })}
        />
      )}

      {/* Export Dialog */}
      {state.showExportDialog && state.worldId && (
        <ExportDialog
          worldId={state.worldId}
          chapters={[] /* TODO: populate from full chapter list when available in state */}
          onClose={() => dispatch({ type: 'SET_SHOW_EXPORT_DIALOG', show: false })}
        />
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
