import { useEffect, useRef, useState } from 'react';
import { api } from './api/client';
import styles from './App.module.css';
import { AppStateProvider, useAppState } from './AppState';
import ConnectionBanner from './components/ConnectionBanner';
import ErrorBoundary from './components/ErrorBoundary';
import InspectorPanel from './components/InspectorPanel';
import MainPanel from './components/MainPanel';
import OnboardingModal from './components/OnboardingModal';
import Sidebar from './components/Sidebar';
import Skeleton from './components/Skeleton';
import { ToastProvider } from './components/Toast';
import { useSSE } from './hooks/useSSE';

function AppInner() {
  const { state, dispatch } = useAppState();
  const [sidebarOpen, setSidebarOpen] = useState(window.innerWidth >= 768);
  const [inspectorOpen, setInspectorOpen] = useState(window.innerWidth >= 1180);
  const [bootstrapped, setBootstrapped] = useState(false);
  const [guideOpen, setGuideOpen] = useState(false);

  useEffect(() => {
    try {
      if (window.localStorage.getItem('merak.webui.onboarding.seen') !== 'true') {
        setGuideOpen(true);
      }
    } catch {
      setGuideOpen(true);
    }
  }, []);

  function closeGuide() {
    setGuideOpen(false);
    try {
      window.localStorage.setItem('merak.webui.onboarding.seen', 'true');
    } catch {
      /* ignore storage failures */
    }
  }

  useEffect(() => {
    Promise.allSettled([
      api.metadata(),
      api.listSessions(),
      api.listWorlds(),
      api.capabilities(),
      api.listWorkspaceFiles({ session_id: state.sessionId, world_id: state.worldId ?? undefined }),
    ]).then(
      ([metadataRes, sessionsRes, worldsRes, capabilitiesRes, filesRes]) => {
        if (metadataRes.status === 'fulfilled') {
          dispatch({ type: 'SET_METADATA', metadata: metadataRes.value });
        }
        if (sessionsRes.status === 'fulfilled') {
          dispatch({ type: 'SET_SESSIONS', sessions: sessionsRes.value.sessions ?? [] });
        }
        if (worldsRes.status === 'fulfilled') {
          dispatch({ type: 'SET_WORLDS', worlds: worldsRes.value.worlds ?? [] });
        }
        if (capabilitiesRes.status === 'fulfilled') {
          dispatch({
            type: 'SET_CAPABILITIES',
            capabilities: capabilitiesRes.value.capabilities,
            fallback: capabilitiesRes.value.fallback,
          });
        }
        if (filesRes.status === 'fulfilled') {
          dispatch({
            type: 'SET_WORKSPACE_FILES',
            files: filesRes.value.files,
            root: filesRes.value.root,
            fallback: filesRes.value.fallback,
          });
        }
        setBootstrapped(true);
      },
    );
  }, [dispatch]);

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
      api.listWorkspaceFiles({ session_id: state.sessionId, world_id: state.worldId }),
    ]).then(([overviewRes, worldsRes, agentsRes, foreshadowingRes, secretsRes, timeRes, filesRes]) => {
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

      if (filesRes.status === 'fulfilled') {
        dispatch({
          type: 'SET_WORKSPACE_FILES',
          files: filesRes.value.files,
          root: filesRes.value.root,
          fallback: filesRes.value.fallback,
        });
      }

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
  }, [state.worldId, state.sessionId, dispatch]);

  const creatingRef = useRef(false);

  useEffect(() => {
    if (!state.sessionId) {
      if (creatingRef.current) return;
      creatingRef.current = true;
      api
        .createSession()
        .then((data) => {
          const id = data.session_id as string;
          dispatch({ type: 'SET_SESSION', sessionId: id });
        })
        .catch((e) => {
          creatingRef.current = false;
          console.error('Failed to create session:', e);
        });
      return;
    }
    api
      .listSessions()
      .then((data) => {
        dispatch({ type: 'SET_SESSIONS', sessions: data.sessions ?? [] });
      })
      .catch((e) => {
        console.error('Failed to list sessions:', e);
      });
  }, [state.sessionId, dispatch]);

  const sseUrl = state.sessionId ? api.sseUrl(state.sessionId) : null;

  const connState = useSSE(sseUrl, dispatch, state.lastSeq);

  const isLoading = !bootstrapped;

  if (isLoading) {
    return <Skeleton />;
  }

  return (
    <ToastProvider>
      <div className={styles.layout}>
        <ErrorBoundary>
          <Sidebar open={sidebarOpen} onClose={() => setSidebarOpen(false)} />
        </ErrorBoundary>
        <ErrorBoundary>
          <div className={styles.workspace}>
            <ConnectionBanner state={connState} />
            <MainPanel
              onToggleSidebar={() => setSidebarOpen((prev) => !prev)}
              onToggleInspector={() => setInspectorOpen((prev) => !prev)}
              onOpenGuide={() => setGuideOpen(true)}
              sidebarOpen={sidebarOpen}
              inspectorOpen={inspectorOpen}
              connectionState={connState}
            />
          </div>
        </ErrorBoundary>
        <ErrorBoundary>
          <InspectorPanel open={inspectorOpen} onClose={() => setInspectorOpen(false)} />
        </ErrorBoundary>
        {guideOpen && <OnboardingModal onClose={closeGuide} />}
      </div>
    </ToastProvider>
  );
}

export default function App() {
  return (
    <AppStateProvider>
      <AppInner />
    </AppStateProvider>
  );
}
