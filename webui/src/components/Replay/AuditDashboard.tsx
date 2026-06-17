import { useEffect, useMemo, useState } from 'react';
import { api } from '../../api/client';
import type { RunAudit } from '../../api/types';
import { useAppState } from '../../AppState';
import { useI18n } from '../../i18n';
import styles from './AuditDashboard.module.css';

export default function AuditDashboard() {
  const { state } = useAppState();
  const { t } = useI18n();
  const auditRunId = state.currentRun ?? state.lastRunId;
  const [audit, setAudit] = useState<RunAudit | null>(null);
  const [auditStatus, setAuditStatus] = useState<'idle' | 'loading' | 'ready' | 'unavailable'>('idle');

  useEffect(() => {
    if (!auditRunId) {
      setAudit(null);
      setAuditStatus('idle');
      return;
    }
    let cancelled = false;
    setAuditStatus('loading');
    api
      .fetchRunAudit(auditRunId)
      .then((res) => {
        if (!cancelled) {
          setAudit(res.audit);
          setAuditStatus('ready');
        }
      })
      .catch(() => {
        if (!cancelled) {
          setAudit(null);
          setAuditStatus('unavailable');
        }
      });
    return () => {
      cancelled = true;
    };
  }, [auditRunId]);

  const stats = useMemo(() => {
    const toolCounts: Record<string, number> = {};
    let approvalCount = 0;
    let fileCount = 0;
    let stateCount = 0;

    state.runTimeline.forEach((item) => {
      if (item.type === 'tool') {
        const name = item.label.replace('Tool: ', '');
        toolCounts[name] = (toolCounts[name] ?? 0) + 1;
      } else if (item.type === 'approval') {
        approvalCount++;
      } else if (item.type === 'file') {
        fileCount++;
      } else {
        stateCount++;
      }
    });

    const sortedTools = Object.entries(toolCounts).sort((a, b) => b[1] - a[1]);
    return {
      totalSteps: state.runTimeline.length,
      toolCalls: Object.values(toolCounts).reduce((s, c) => s + c, 0),
      approvalCount,
      fileCount,
      stateCount,
      topTools: sortedTools.slice(0, 5),
    };
  }, [state.runTimeline]);

  const totalTokens = audit
    ? audit.token_usage.input + audit.token_usage.output
    : state.usage.inputTokens + state.usage.outputTokens;
  const model = state.metadata?.models?.find((m) => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const usagePct = Math.min(100, Math.round((totalTokens / budget) * 100));

  const sourceLabel =
    auditStatus === 'loading'
      ? t('audit.source.loading')
      : auditStatus === 'ready'
        ? t('audit.source.backend')
        : auditStatus === 'unavailable'
          ? t('audit.source.unavailable')
          : t('audit.source.local');

  if (state.runTimeline.length === 0 && !auditRunId) {
    return <p className={styles.empty}>{t('audit.empty')}</p>;
  }

  return (
    <div className={styles.container}>
      <div className={styles.header}>Run Audit</div>

      <div
        className={`${styles.sourceBanner} ${auditStatus === 'ready' ? styles.sourceBackend : ''}`}
      >
        <span className={styles.sourceLabel}>{sourceLabel}</span>
      </div>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>{t('audit.overview')}</div>
        <div className={styles.statsGrid}>
          <div className={styles.stat}>
            <span className={styles.statValue}>{audit?.turn_count ?? stats.totalSteps}</span>
            <span className={styles.statLabel}>
              {audit ? t('audit.turns') : t('audit.totalSteps')}
            </span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>
              {audit
                ? Object.values(audit.tool_call_stats).reduce((sum, count) => sum + count, 0)
                : stats.toolCalls}
            </span>
            <span className={styles.statLabel}>{t('audit.toolCalls')}</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>{audit?.approval_count ?? stats.approvalCount}</span>
            <span className={styles.statLabel}>{t('audit.approvalCount')}</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>
              {audit ? `${audit.duration_seconds}s` : stats.fileCount}
            </span>
            <span className={styles.statLabel}>
              {audit ? t('audit.duration') : t('audit.fileCount')}
            </span>
          </div>
        </div>
      </section>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>{t('audit.tokenUsage')}</div>
        <div className={styles.tokenBar}>
          <div
            className={`${styles.tokenFill} ${usagePct > 90 ? styles.tokenWarn : ''}`}
            style={{ width: `${Math.max(2, usagePct)}%` }}
          />
        </div>
        <div className={styles.tokenRow}>
          <span>In: {((audit?.token_usage.input ?? state.usage.inputTokens) / 1000).toFixed(1)}K</span>
          <span>Out: {((audit?.token_usage.output ?? state.usage.outputTokens) / 1000).toFixed(1)}K</span>
          <span>Budget: {(budget / 1000).toFixed(0)}K</span>
        </div>
      </section>

      {(audit || stats.topTools.length > 0) && (
        <section className={styles.section}>
          <div className={styles.sectionTitle}>{t('audit.toolDistribution')}</div>
          {(audit
            ? Object.entries(audit.tool_call_stats)
                .sort((a, b) => b[1] - a[1])
                .slice(0, 5)
            : stats.topTools
          ).map(([name, count]) => (
            <div key={name} className={styles.toolRow}>
              <span className={styles.toolName}>{name}</span>
              <span className={styles.toolBar}>
                <span
                  className={styles.toolBarFill}
                  style={{
                    width: `${Math.max(
                      5,
                      (count /
                        Math.max(
                          1,
                          audit
                            ? Object.values(audit.tool_call_stats).reduce(
                                (sum, value) => sum + value,
                                0,
                              )
                            : stats.toolCalls,
                        )) *
                        100,
                    )}%`,
                  }}
                />
              </span>
              <span className={styles.toolCount}>{count}</span>
            </div>
          ))}
        </section>
      )}

      <section className={styles.section}>
        <div className={styles.sectionTitle}>{t('audit.runStatus')}</div>
        <div className={styles.statusRow}>
          <span>Status</span>
          <span className={styles.statusValue}>
            {audit?.status ?? state.status.replace(/_/g, ' ')}
          </span>
        </div>
        <div className={styles.statusRow}>
          <span>Model</span>
          <span className={styles.statusValue}>
            {state.selectedModel || state.metadata?.model || '—'}
          </span>
        </div>
        <div className={styles.statusRow}>
          <span>Output files</span>
          <span className={styles.statusValue}>{state.workspaceFiles.length}</span>
        </div>
      </section>
    </div>
  );
}
