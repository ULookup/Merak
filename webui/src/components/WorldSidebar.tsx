import { useState } from 'react';
import {
  ArrowLeft,
  BookOpen,
  Crown,
  FileText,
  Map,
  PenLine,
  ScrollText,
  Settings,
  Sparkles,
  Swords,
  UserRound,
  UsersRound,
  X,
} from 'lucide-react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import BrandMark from './BrandMark';
import ContextMeter from './Sidebar/ContextMeter';
import ModelSelector from './Sidebar/ModelSelector';
import PipelineNavigator from './Sidebar/PipelineNavigator';
import WorkflowMonitor from './Sidebar/WorkflowMonitor';
import SessionList from './Sidebar/SessionList';
import SettingsPanel from './Sidebar/SettingsPanel';
import WorldSelector from './Sidebar/WorldSelector';
import type { WorldAgent } from '../api/types';
import styles from './WorldSidebar.module.css';

interface WorldSidebarProps {
  open?: boolean;
  onClose: () => void;
}

function AgentIcon({ kind }: { kind: string }) {
  const common = { size: 15, 'aria-hidden': true as const, strokeWidth: 2.3 };
  if (kind === 'god' || kind === '0' || kind === 'ruler') return <Crown {...common} />;
  if (kind === 'map_manager' || kind === '1' || kind === 'cartographer') return <Map {...common} />;
  if (kind === 'history_manager' || kind === '2' || kind === 'scribe') return <ScrollText {...common} />;
  if (kind === 'magic_system_manager' || kind === '3' || kind === 'oracle') return <Sparkles {...common} />;
  if (kind === 'faction_manager' || kind === '4' || kind === 'warrior') return <Swords {...common} />;
  if (kind === 'group' || kind === '6' || kind === 'collective') return <UsersRound {...common} />;
  return <UserRound {...common} />;
}

export default function WorldSidebar({ open = true, onClose }: WorldSidebarProps) {
  const { state, dispatch } = useAppState();
  const [settingsOpen, setSettingsOpen] = useState(false);

  const currentWorld = state.worlds.find((w) => w.id === state.worldId);

  function handleBackToDashboard() {
    dispatch({ type: 'SET_APP_PHASE', phase: 'no_agent' });
  }

  const overview = state.storyOverview;

  return (
    <aside className={styles.sidebar}>
      {open && <div className={styles.overlay} onClick={onClose} />}
      <button className={styles.closeBtn} onClick={onClose} aria-label="Close sidebar">
        <X size={16} aria-hidden="true" strokeWidth={2.4} />
      </button>

      <div className={styles.brandSection}>
        <BrandMark />
      </div>

      {/* World Header — click to go back to dashboard */}
      <div className={styles.worldHeader} onClick={handleBackToDashboard} role="button" tabIndex={0}
        onKeyDown={(e) => { if (e.key === 'Enter') handleBackToDashboard(); }}>
        <ArrowLeft size={15} aria-hidden="true" strokeWidth={2.4} />
        <span className={styles.worldHeaderName}>
          {currentWorld?.name ?? 'Select World'}
        </span>
      </div>

      <WorldSelector />

      <PipelineNavigator />
      <WorkflowMonitor />

      {/* Story Tree — from storyOverview (current arc / chapter / scene) */}
      {overview && (overview.current_arc || overview.current_chapter || overview.current_scene) && (
        <div className={styles.storyTreeSection}>
          <div className={styles.storyTreeLabel}>Story Tree</div>
          {overview.current_arc && (
            <div>
              <div className={styles.storyTreeItem}>
                <BookOpen size={13} aria-hidden="true" />
                {overview.current_arc.title ?? overview.current_arc.id}
              </div>
              {overview.current_chapter && (
                <div>
                  <div className={`${styles.storyTreeItem} ${styles.storyTreeChild}`}>
                    <FileText size={13} aria-hidden="true" />
                    {overview.current_chapter.title ?? overview.current_chapter.id}
                  </div>
                  {overview.current_scene && (
                    <div
                      className={`${styles.storyTreeItem} ${styles.storyTreeChild}`}
                      style={{ paddingLeft: '2.5rem' }}
                      onClick={() => dispatch({ type: 'SET_INSPECTOR_TAB', tab: 'story' })}
                    >
                      <PenLine size={13} aria-hidden="true" />
                      {overview.current_scene.title ?? overview.current_scene.id}
                    </div>
                  )}
                </div>
              )}
            </div>
          )}
        </div>
      )}

      {/* Agent List — quick-switch between agents */}
      {state.agents.length > 0 && (
        <div className={styles.agentListSection}>
          <div className={styles.agentListLabel}>Agents</div>
          {state.agents.map((agent) => {
            const kind = typeof agent.kind === 'string' ? agent.kind : '';
            async function handleAgentClick() {
              try {
                const res = await api.getOrCreateAgentSession(state.worldId!, agent.id);
                dispatch({
                  type: 'SET_AGENT_SESSION',
                  sessionId: res.session.id,
                  agentId: agent.id,
                });
              } catch (e) {
                console.error('Failed to switch agent session:', e);
              }
            }
            return (
              <div
                key={agent.id}
                className={
                  agent.id === state.agentId
                    ? styles.agentListItemActive
                    : styles.agentListItem
                }
                onClick={handleAgentClick}
                role="button"
                tabIndex={0}
                onKeyDown={(e) => { if (e.key === 'Enter') handleAgentClick(); }}
              >
                <span className={styles.agentIcon}><AgentIcon kind={kind} /></span>
                <span>{agent.name ?? agent.id}</span>
              </div>
            );
          })}
        </div>
      )}

      <SessionList worldId={state.worldId ?? undefined} agentId={state.agentId ?? undefined} />

      <ModelSelector />
      <ContextMeter />

      <div
        className={styles.settingsTrigger}
        onClick={() => setSettingsOpen((prev) => !prev)}
        role="button"
        tabIndex={0}
        onKeyDown={(e) => { if (e.key === 'Enter') setSettingsOpen((prev) => !prev); }}
      >
        <Settings size={15} aria-hidden="true" strokeWidth={2.3} />
        Settings
      </div>
      {settingsOpen && <SettingsPanel />}
    </aside>
  );
}
