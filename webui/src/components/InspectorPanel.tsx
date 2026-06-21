import { lazy, Suspense } from 'react';
import { X } from 'lucide-react';
import { useAppState, type InspectorTab } from '../AppState';
import { useI18n } from '../i18n';
import styles from './InspectorPanel.module.css';

const AgentsInspector = lazy(() => import('./Inspector/AgentsInspector'));
const CreationDashboard = lazy(() => import('./Inspector/CreationDashboard'));
const FilesInspector = lazy(() => import('./Inspector/FilesInspector'));
const RunInspector = lazy(() => import('./Inspector/RunInspector'));
const StoryInspector = lazy(() => import('./Inspector/StoryInspector'));

function TabFallback() {
  return <div className={styles.empty} style={{ minHeight: 180 }} />;
}

interface InspectorPanelProps {
  open?: boolean;
  onClose?: () => void;
}

const tabs: Array<{ id: InspectorTab; labelKey: string }> = [
  { id: 'story', labelKey: 'inspector.story' },
  { id: 'files', labelKey: 'inspector.files' },
  { id: 'agents', labelKey: 'inspector.agents' },
  { id: 'creation', labelKey: 'inspector.creation' },
  { id: 'run', labelKey: 'inspector.run' },
];

function EmptyState({ tab }: { tab: InspectorTab }) {
  const { t } = useI18n();
  if (tab === 'story') return <div className={styles.empty}>{t('inspector.emptyStory')}</div>;
  if (tab === 'agents')
    return <div className={styles.empty}>Select a world to see agent voices.</div>;
  return null;
}

function titleKeyForTab(tab: InspectorTab) {
  if (tab === 'files') return 'inspector.filesTitle';
  if (tab === 'agents') return 'inspector.agentsTitle';
  if (tab === 'creation') return 'inspector.creationTitle';
  if (tab === 'run') return 'inspector.runTitle';
  return 'inspector.storyTitle';
}

export default function InspectorPanel({ open = true, onClose }: InspectorPanelProps) {
  const { state, dispatch } = useAppState();
  const { t } = useI18n();

  return (
    <aside
      id="session-inspector-panel"
      className={`${styles.panel} ${open ? styles.panelOpen : ''}`}
      aria-label="Story inspector"
      aria-hidden={!open}
      data-testid="inspector-panel"
    >
      <div className={styles.mobileScrim} onClick={onClose} />
      <div className={styles.surface}>
        <header className={styles.header}>
          <div>
            <div className={styles.kicker}>{t('inspector.kicker')}</div>
            <h2>{t(titleKeyForTab(state.inspectorTab))}</h2>
          </div>
          <button className={styles.closeBtn} onClick={onClose} aria-label="Close inspector">
            <X size={16} aria-hidden="true" strokeWidth={2.4} />
          </button>
        </header>

        <div className={styles.tabs} role="tablist" aria-label="Inspector tabs">
          {tabs.map((tab) => (
            <button
              key={tab.id}
              className={`${styles.tab} ${state.inspectorTab === tab.id ? styles.tabActive : ''}`}
              onClick={() => dispatch({ type: 'SET_INSPECTOR_TAB', tab: tab.id })}
              role="tab"
              aria-selected={state.inspectorTab === tab.id}
            >
              {t(tab.labelKey)}
            </button>
          ))}
        </div>

        {!state.worldId && state.inspectorTab !== 'files' && state.inspectorTab !== 'run' && state.inspectorTab !== 'creation' ? (
          <EmptyState tab={state.inspectorTab} />
        ) : (
          <div className={styles.content}>
            <Suspense fallback={<TabFallback />}>
              {state.inspectorTab === 'story' && <StoryInspector />}
              {state.inspectorTab === 'files' && <FilesInspector />}
              {state.inspectorTab === 'agents' && <AgentsInspector />}
              {state.inspectorTab === 'creation' && <CreationDashboard />}
              {state.inspectorTab === 'run' && <RunInspector />}
            </Suspense>
          </div>
        )}
      </div>
    </aside>
  );
}
