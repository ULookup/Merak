import { useState } from 'react';
import { Activity, AlertTriangle, Brain, Clock3, FileText, Wrench } from 'lucide-react';
import { useAppState } from '../../AppState';
import RunReplay from '../Replay/RunReplay';
import AuditDashboard from '../Replay/AuditDashboard';
import styles from '../InspectorPanel.module.css';

type RunTab = 'live' | 'replay' | 'audit';

function timeLabel(value: number) {
  return new Intl.DateTimeFormat(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  }).format(value);
}

export default function RunInspector() {
  const { state } = useAppState();
  const [subTab, setSubTab] = useState<RunTab>('live');
  const used = state.usage.inputTokens + state.usage.outputTokens;
  const model = state.metadata?.models?.find((m) => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const pct = Math.min(100, Math.round((used / budget) * 100));

  const subTabs: Array<{ id: RunTab; label: string }> = [
    { id: 'live', label: 'Live' },
    { id: 'replay', label: 'Replay' },
    { id: 'audit', label: 'Audit' },
  ];

  return (
    <>
      <div className={styles.subTabs}>
        {subTabs.map(t => (
          <button
            key={t.id}
            className={`${styles.subTab} ${subTab === t.id ? styles.subTabActive : ''}`}
            onClick={() => setSubTab(t.id)}
          >
            {t.label}
          </button>
        ))}
      </div>

      {subTab === 'live' && (
        <>
          <section className={styles.runCard}>
            <span className={styles.pulse}>
              <Activity size={14} aria-hidden="true" strokeWidth={2.4} />
            </span>
            <div>
              <div className={styles.sectionTitle}>Current Run</div>
              <strong>{state.status.replace(/_/g, ' ')}</strong>
              <p>{state.currentRun ?? 'No active run'}</p>
            </div>
          </section>

          <section className={styles.section}>
            <div className={styles.sectionTitle}>Runtime Signals</div>
            <div className={styles.signalGrid}>
              <span>
                <Brain size={14} aria-hidden="true" />
                {state.selectedModel || state.metadata?.model || 'Default model'}
              </span>
              <span>
                <Wrench size={14} aria-hidden="true" />
                {(state.metadata?.tools ?? []).length} tools
              </span>
              <span>
                <FileText size={14} aria-hidden="true" />
                {state.workspaceFiles.length} files
              </span>
              <span>
                <Clock3 size={14} aria-hidden="true" />
                {state.metadata?.delegation_patterns?.join(' / ') || 'single run'}
              </span>
            </div>
          </section>

          <section className={styles.section}>
            <div className={styles.sectionTitle}>Context Health</div>
            <div className={styles.meter}>
              <div style={{ width: `${pct}%` }} />
            </div>
            <p className={styles.muted}>
              {(used / 1000).toFixed(1)}K / {(budget / 1000).toFixed(0)}K tokens
            </p>
          </section>

          <section className={styles.section}>
            <div className={styles.sectionTitle}>Run Timeline</div>
            {state.runTimeline.length === 0 ? (
              <p className={styles.muted}>Run steps, tool calls, approvals, and file events will land here.</p>
            ) : (
              <div className={styles.timelineList}>
                {state.runTimeline.map((item) => (
                  <article key={item.id} className={styles.timelineItem}>
                    <span>{timeLabel(item.at)}</span>
                    <strong>{item.label}</strong>
                    {item.detail && <code>{item.detail}</code>}
                  </article>
                ))}
              </div>
            )}
          </section>
        </>
      )}

      {subTab === 'replay' && <RunReplay />}
      {subTab === 'audit' && <AuditDashboard />}

      {state.worldbuildingStatus === 'error' && (
        <div className={styles.error}>
          <AlertTriangle size={14} aria-hidden="true" strokeWidth={2.4} />
          {state.worldbuildingError}
        </div>
      )}
    </>
  );
}
