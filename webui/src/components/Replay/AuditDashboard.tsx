import { useMemo } from 'react';
import { useAppState } from '../../AppState';
import styles from './AuditDashboard.module.css';

export default function AuditDashboard() {
  const { state } = useAppState();

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

  const totalTokens = state.usage.inputTokens + state.usage.outputTokens;
  const model = state.metadata?.models?.find(m => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const usagePct = Math.min(100, Math.round((totalTokens / budget) * 100));

  if (state.runTimeline.length === 0) {
    return <p className={styles.empty}>暂无运行数据。开始一次运行后这里会显示统计。</p>;
  }

  return (
    <div className={styles.container}>
      <div className={styles.header}>运行审计</div>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>概览</div>
        <div className={styles.statsGrid}>
          <div className={styles.stat}>
            <span className={styles.statValue}>{stats.totalSteps}</span>
            <span className={styles.statLabel}>总步骤</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>{stats.toolCalls}</span>
            <span className={styles.statLabel}>工具调用</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>{stats.approvalCount}</span>
            <span className={styles.statLabel}>审批请求</span>
          </div>
          <div className={styles.stat}>
            <span className={styles.statValue}>{stats.fileCount}</span>
            <span className={styles.statLabel}>文件产出</span>
          </div>
        </div>
      </section>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>Token 用量</div>
        <div className={styles.tokenBar}>
          <div className={styles.tokenFill} style={{ width: `${usagePct}%` }} />
        </div>
        <div className={styles.tokenRow}>
          <span>输入：{(state.usage.inputTokens / 1000).toFixed(1)}K</span>
          <span>输出：{(state.usage.outputTokens / 1000).toFixed(1)}K</span>
          <span>上限：{(budget / 1000).toFixed(0)}K</span>
        </div>
      </section>

      {stats.topTools.length > 0 && (
        <section className={styles.section}>
          <div className={styles.sectionTitle}>工具调用分布</div>
          {stats.topTools.map(([name, count]) => (
            <div key={name} className={styles.toolRow}>
              <span className={styles.toolName}>{name}</span>
              <span className={styles.toolBar}>
                <span
                  className={styles.toolBarFill}
                  style={{ width: `${Math.max(5, (count / stats.toolCalls) * 100)}%` }}
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
          <span className={styles.statusValue}>{state.status.replace(/_/g, ' ')}</span>
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
