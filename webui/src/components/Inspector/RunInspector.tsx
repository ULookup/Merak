import { useState } from 'react';
import { Activity, AlertTriangle, Brain, Clock3, FileText, Plug, Wrench } from 'lucide-react';
import { useAppState } from '../../AppState';
import { useI18n } from '../../i18n';
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
  const { t } = useI18n();
  const { state } = useAppState();
  const [subTab, setSubTab] = useState<RunTab>('live');
  const used = state.usage.inputTokens + state.usage.outputTokens;
  const model = state.metadata?.models?.find((m) => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const pct = Math.min(100, Math.round((used / budget) * 100));

  const subTabs: Array<{ id: RunTab; label: string }> = [
    { id: 'live', label: t('run.tab.live') },
    { id: 'replay', label: t('run.tab.replay') },
    { id: 'audit', label: t('run.tab.audit') },
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
              <div className={styles.sectionTitle}>{t('run.current')}</div>
              <strong>{t(`status.${state.status}.title`)}</strong>
              <p>{state.currentRun ? t('run.active') : t('run.none')}</p>
            </div>
          </section>

          <section className={styles.section}>
            <div className={styles.sectionTitle}>{t('run.signals')}</div>
            <div className={styles.signalGrid}>
              <span>
                <Brain size={14} aria-hidden="true" />
                {state.selectedModel || state.metadata?.model || t('status.defaultAssistant')}
              </span>
              <span>
                <Wrench size={14} aria-hidden="true" />
                {t('run.abilitiesCount').replace('{count}', String((state.metadata?.tools ?? []).length))}
              </span>
              <span>
                <FileText size={14} aria-hidden="true" />
                {t('run.draftsCount').replace('{count}', String(state.workspaceFiles.length))}
              </span>
              <span>
                <Clock3 size={14} aria-hidden="true" />
                {state.metadata?.delegation_patterns?.length
                  ? t('run.collaborationReady')
                  : t('run.singleAssistant')}
              </span>
            </div>
          </section>

          <section className={styles.section}>
            <div className={styles.sectionTitle}>{t('run.abilities')}</div>
            {!state.metadata?.tools || state.metadata.tools.length === 0 ? (
              <p className={styles.muted}>{t('run.noAbilities')}</p>
            ) : (
              <div className={styles.toolList}>
                {state.metadata.tools.map((tool) => (
                  <div key={tool.name} className={styles.toolRow}>
                    <span className={styles.toolSource}>
                      {tool.source === 'mcp' ? (
                        <Plug size={13} aria-hidden="true" strokeWidth={2.3} />
                      ) : (
                        <Wrench size={13} aria-hidden="true" strokeWidth={2.3} />
                      )}
                    </span>
                    <span className={styles.toolName}>{tool.name}</span>
                    <span className={styles.toolBadge}>{tool.source ?? 'built-in'}</span>
                  </div>
                ))}
              </div>
            )}
          </section>

          <section className={styles.section}>
            <div className={styles.sectionTitle}>{t('run.context')}</div>
            <div className={styles.meter}>
              <div style={{ width: `${pct}%` }} />
            </div>
            <p className={styles.muted}>
              {(used / 1000).toFixed(1)}K / {(budget / 1000).toFixed(0)}K {t('status.contextUnit')}
            </p>
          </section>

          <section className={styles.section}>
            <div className={styles.sectionTitle}>{t('run.timeline')}</div>
            {state.runTimeline.length === 0 ? (
              <p className={styles.muted}>{t('run.emptyTimeline')}</p>
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
