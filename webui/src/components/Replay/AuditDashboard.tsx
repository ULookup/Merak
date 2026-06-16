import { useEffect, useMemo, useState } from 'react';
import { api, formatApiError } from '../../api/client';
import type { RunAudit } from '../../api/types';
import { useAppState } from '../../AppState';
import styles from './AuditDashboard.module.css';

export default function AuditDashboard() {
  const { state } = useAppState();
  const auditRunId = state.currentRun ?? state.lastRunId;
  const [audit, setAudit] = useState<RunAudit | null>(null);
  const [auditStatus, setAuditStatus] = useState<'idle' | 'loading' | 'ready' | 'error'>('idle');
  const [auditError, setAuditError] = useState<string | null>(null);

  useEffect(() => {
    if (!auditRunId) {
      setAudit(null);
      setAuditStatus('idle');
      setAuditError(null);
      return;
    }
    let cancelled = false;
    setAuditStatus('loading');
    setAuditError(null);
    api.fetchRunAudit(auditRunId)
      .then((res) => {
        if (!cancelled) {
          setAudit(res.audit);
          setAuditStatus('ready');
        }
      })
      .catch((error) => {
        if (!cancelled) {
          setAudit(null);
          setAuditStatus('error');
          setAuditError(formatApiError(error, '后端审计不可用；下方仅显示当前界面的运行记录。'));
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

    state.runTimeline.forEach(item => {
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
  const model = state.metadata?.models?.find(m => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const usagePct = Math.min(100, Math.round((totalTokens / budget) * 100));

  if (state.runTimeline.length === 0 && !auditRunId) {
    return <p className={styles.empty}>暂无运行记录。开始一次创作后，这里会显示步骤、工具和 token 统计。</p>;
  }

  return (
    <div className={styles.container}>
      <div className={styles.header}>运行审计</div>

      <div className={`${styles.sourceBanner} ${auditStatus === 'ready' ? styles.sourceBackend : ''}`}>
        {auditStatus === 'loading' && '正在读取后端运行审计...'}
        {auditStatus === 'ready' && `后端审计 · ${audit?.run_id}`}
        {auditStatus === 'error' && auditError}
        {auditStatus === 'idle' && '当前界面运行记录'}
      </div>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>概览</div>
        <div className={styles.statsGrid}>
          <div className={styles.stat}>
            <span className={styles.statValue}>{audit?.turn_count ?? stats.totalSteps}</span>
            <span className={styles.statLabel}>{audit ? 'Turns' : '总步骤'}</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>
              {audit
                ? Object.values(audit.tool_call_stats).reduce((sum, count) => sum + count, 0)
                : stats.toolCalls}
            </span>
            <span className={styles.statLabel}>工具调用</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>{audit?.approval_count ?? stats.approvalCount}</span>
            <span className={styles.statLabel}>审批请求</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>
              {audit ? `${audit.duration_seconds}s` : stats.fileCount}
            </span>
            <span className={styles.statLabel}>{audit ? '耗时' : '文件产出'}</span>
          </div>
        </div>
      </section>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>Token 用量</div>
        <div className={styles.tokenBar}>
          <div className={styles.tokenFill} style={{ width: `${usagePct}%` }} />
        </div>
        <div className={styles.tokenRow}>
          <span>输入：{((audit?.token_usage.input ?? state.usage.inputTokens) / 1000).toFixed(1)}K</span>
          <span>输出：{((audit?.token_usage.output ?? state.usage.outputTokens) / 1000).toFixed(1)}K</span>
          <span>上限：{(budget / 1000).toFixed(0)}K</span>
        </div>
      </section>

      {(audit || stats.topTools.length > 0) && (
        <section className={styles.section}>
          <div className={styles.sectionTitle}>工具调用分布</div>
          {(audit
            ? Object.entries(audit.tool_call_stats).sort((a, b) => b[1] - a[1]).slice(0, 5)
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
                            ? Object.values(audit.tool_call_stats).reduce((sum, value) => sum + value, 0)
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
        <div className={styles.sectionTitle}>运行状态</div>
        <div className={styles.statusRow}>
          <span>状态</span>
          <span className={styles.statusValue}>{audit?.status ?? state.status.replace(/_/g, ' ')}</span>
        </div>
        <div className={styles.statusRow}>
          <span>当前模型</span>
          <span className={styles.statusValue}>{state.selectedModel || state.metadata?.model || '—'}</span>
        </div>
        <div className={styles.statusRow}>
          <span>输出文件</span>
          <span className={styles.statusValue}>{state.workspaceFiles.length}</span>
        </div>
      </section>
    </div>
  );
}
