import { useEffect, useState } from 'react';
import { useAppState } from '../../AppState';
import type { WorkflowSummary } from '../../api/types';
import { activatePipelineWorkflow, listPipelineWorkflows } from '../../api/client';
import styles from './WorkflowMonitor.module.css';

export default function WorkflowMonitor() {
  const { state, dispatch } = useAppState();
  const conditions = state.pipelineConditions ?? [];
  const history = state.pipelineHistory ?? [];
  const metCount = conditions.filter(c => c.met).length;
  const total = conditions.length;
  const pct = total > 0 ? Math.round((metCount / total) * 100) : 0;

  const [workflows, setWorkflows] = useState<WorkflowSummary[]>([]);
  const [workflowsLoading, setWorkflowsLoading] = useState(false);
  const [workflowsError, setWorkflowsError] = useState<string | null>(null);
  const [selectedWorkflow, setSelectedWorkflow] = useState(state.pipelineActiveWorkflow || '');

  useEffect(() => {
    let cancelled = false;
    setWorkflowsLoading(true);
    setWorkflowsError(null);
    listPipelineWorkflows()
      .then(data => { if (!cancelled) setWorkflows(data); })
      .catch(err => { if (!cancelled) setWorkflowsError(err.message); })
      .finally(() => { if (!cancelled) setWorkflowsLoading(false); });
    return () => { cancelled = true; };
  }, []);

  const handleWorkflowChange = async (name: string) => {
    setSelectedWorkflow(name);
    try {
      await activatePipelineWorkflow(state.worldId!, name);
      dispatch({ type: 'PIPELINE_WORKFLOW_ACTIVATED', name });
    } catch (err) {
      alert(`Failed to activate workflow: ${err instanceof Error ? err.message : 'Unknown error'}`);
    }
  };

  return (
    <div className={styles.container}>
      <div className={styles.title}>工作流状态</div>

      {workflowsLoading && <div className={styles.status}>Loading workflows...</div>}
      {workflowsError && <div className={styles.status}>Error: {workflowsError}</div>}
      {!workflowsLoading && !workflowsError && workflows.length > 1 && (
        <select
          className={styles.workflowSelect}
          value={selectedWorkflow}
          onChange={e => handleWorkflowChange(e.target.value)}
        >
          {workflows.map(w => (
            <option key={w.name} value={w.name}>{w.description || w.name}</option>
          ))}
        </select>
      )}

      {total > 0 && (
        <div className={styles.section}>
          <div className={styles.progressHeader}>
            <span>条件进度</span>
            <span>{metCount}/{total} ({pct}%)</span>
          </div>
          <div className={styles.progressBar}>
            <div className={styles.progressFill} style={{ width: `${pct}%` }} />
          </div>
          <div className={styles.conditionList}>
            {conditions.map((c, i) => (
              <div key={i} className={styles.conditionItem}>
                <span className={c.met ? styles.checkGreen : styles.checkGray}>
                  {c.met ? '✓' : '○'}
                </span>
                <span className={styles.conditionName}>{c.name}</span>
                {c.current !== undefined && (
                  <span className={styles.conditionProgress}>
                    {c.current}/{c.target}
                  </span>
                )}
              </div>
            ))}
          </div>
        </div>
      )}

      {history.length > 0 && (
        <div className={styles.section}>
          <div className={styles.sectionTitle}>最近转换</div>
          {history.slice(0, 5).map((h) => (
            <div key={h.id} className={styles.historyItem}>
              <span className={styles.historyPhases}>
                {h.from} → {h.to}
              </span>
              <span className={styles.historyTrigger}>{h.trigger}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
