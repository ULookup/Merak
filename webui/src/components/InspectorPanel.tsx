import { X } from 'lucide-react';
import { useAppState, type InspectorTab } from '../AppState';
import AgentsInspector from './Inspector/AgentsInspector';
import FilesInspector from './Inspector/FilesInspector';
import RunInspector from './Inspector/RunInspector';
import StoryInspector from './Inspector/StoryInspector';
import styles from './InspectorPanel.module.css';

interface InspectorPanelProps {
  open?: boolean;
  onClose?: () => void;
}

const tabs: Array<{ id: InspectorTab; label: string }> = [
  { id: 'story', label: 'Story' },
  { id: 'files', label: 'Files' },
  { id: 'agents', label: 'Agents' },
  { id: 'run', label: 'Run' },
];

function EmptyState() {
  return <div className={styles.empty}>Select a world to load story context.</div>;
}

function titleForTab(tab: InspectorTab) {
  if (tab === 'files') return 'Output Files';
  if (tab === 'agents') return 'Agent Voices';
  if (tab === 'run') return 'Run Monitor';
  return 'Story Context';
}

export default function InspectorPanel({ open = true, onClose }: InspectorPanelProps) {
  const { state, dispatch } = useAppState();

  return (
    <aside
      className={`${styles.panel} ${open ? styles.panelOpen : ''}`}
      aria-label="Story inspector"
      data-testid="inspector-panel"
    >
      <div className={styles.mobileScrim} onClick={onClose} />
      <div className={styles.surface}>
        <header className={styles.header}>
          <div>
            <div className={styles.kicker}>Inspector</div>
            <h2>{titleForTab(state.inspectorTab)}</h2>
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
              {tab.label}
            </button>
          ))}
        </div>

        {!state.worldId && state.inspectorTab !== 'files' ? (
          <EmptyState />
        ) : (
          <div className={styles.content}>
            {state.inspectorTab === 'story' && <StoryInspector />}
            {state.inspectorTab === 'files' && <FilesInspector />}
            {state.inspectorTab === 'agents' && <AgentsInspector />}
            {state.inspectorTab === 'run' && <RunInspector />}
          </div>
        )}
      </div>
    </aside>
  );
}
