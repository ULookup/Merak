import { useEffect } from 'react';
import { BookOpen, FileText, PenLine } from 'lucide-react';
import { advancePipeline, getPipelineState } from '../../api/client';
import type { CreativePhase } from '../../api/types';
import { useAppState } from '../../AppState';
import styles from './PipelineNavigator.module.css';

const PHASES: { key: CreativePhase; label: string; desc: string }[] = [
  { key: 'worldbuilding', label: '世界观构建', desc: '整理世界规则与背景' },
  { key: 'character_creation', label: '角色创建', desc: '建立角色声音与关系' },
  { key: 'plot_architecture', label: '情节架构', desc: '梳理章节目标与转折' },
  { key: 'scene_writing', label: '场景写作', desc: '推进正文草稿' },
  { key: 'reflection', label: '复盘修订', desc: '检查连续性并润色' },
];

const VALID_PHASES = new Set<string>(PHASES.map((p) => p.key));

export default function PipelineNavigator() {
  const { state, dispatch } = useAppState();
  const rawPhase = state.pipelinePhase ?? 'worldbuilding';
  const currentPhase: CreativePhase = VALID_PHASES.has(rawPhase)
    ? (rawPhase as CreativePhase)
    : 'worldbuilding';
  const currentIdx = PHASES.findIndex((p) => p.key === currentPhase);
  const conditions = state.pipelineConditions ?? [];
  const nextAllowed = new Set(state.pipelineNextAllowed ?? []);
  const allowedRetreat = new Set(state.pipelineAllowedRetreat ?? []);

  useEffect(() => {
    if (!state.worldId) return;
    let cancelled = false;
    getPipelineState(state.worldId)
      .then((view) => {
        if (!cancelled) dispatch({ type: 'SET_PIPELINE_VIEW', view });
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [state.worldId, dispatch]);

  const handlePhaseClick = async (targetPhase: CreativePhase) => {
    const targetIdx = PHASES.findIndex((p) => p.key === targetPhase);
    if (targetIdx === currentIdx) return;

    const isAdvance = targetIdx > currentIdx;
    const isRetreat = targetIdx < currentIdx;

    if (isAdvance && targetIdx !== currentIdx + 1) return;
    if (isRetreat && !allowedRetreat.has(targetPhase)) return;

    const label = PHASES[targetIdx].label;
    const confirmed = window.confirm(isRetreat ? `回到「${label}」阶段？` : `推进到「${label}」阶段？`);
    if (!confirmed) return;

    try {
      await advancePipeline(state.worldId!, {
        target_phase: targetPhase,
        force: isRetreat,
      });
      dispatch({ type: 'PIPELINE_ERROR_CLEARED' });
    } catch (err) {
      dispatch({
        type: 'PIPELINE_ADVANCE_FAILED',
        reason: err instanceof Error ? err.message : '未知错误',
      });
      alert(`阶段推进失败：${err instanceof Error ? err.message : '未知错误'}`);
    }
  };

  return (
    <div className={styles.container}>
      {state.pipelineAdvanceError && (
        <div className={styles.errorBanner}>
          <span>推进失败：{state.pipelineAdvanceError}</span>
          <button onClick={() => dispatch({ type: 'PIPELINE_ERROR_CLEARED' })}>关闭</button>
        </div>
      )}
      {state.showPhaseAdvancePrompt && (
        <div className={styles.confirmOverlay}>
          <div className={styles.confirmDialog}>
            <p>当前条件已满足，要推进到下一阶段吗？</p>
            <div className={styles.confirmActions}>
              <button
                className={styles.confirmBtn}
                onClick={() => {
                  advancePipeline(state.worldId!, {
                    target_phase: state.showPhaseAdvancePrompt!.nextPhase,
                    force: false,
                  });
                  dispatch({ type: 'CLEAR_PHASE_ADVANCE_PROMPT' });
                }}
              >
                推进
              </button>
              <button
                className={styles.cancelBtn}
                onClick={() => dispatch({ type: 'CLEAR_PHASE_ADVANCE_PROMPT' })}
              >
                取消
              </button>
            </div>
          </div>
        </div>
      )}
      {state.pipelineCycleComplete && (
        <div className={styles.completeBanner}>
          <span>{state.pipelineCycleComplete.message}</span>
          <button onClick={() => dispatch({ type: 'CLEAR_CYCLE_COMPLETE' })}>关闭</button>
        </div>
      )}
      <div className={styles.titleRow}>
        <span className={styles.title}>故事流水线</span>
        {state.pipelineAutoAdvance !== undefined && (
          <span className={state.pipelineAutoAdvance ? styles.badgeAuto : styles.badgeManual}>
            {state.pipelineAutoAdvance ? '自动' : '手动'}
          </span>
        )}
      </div>
      <div className={styles.phases}>
        {PHASES.map((p, i) => {
          const isCurrent = i === currentIdx;
          const isDone = i < currentIdx;
          const isNext = i === currentIdx + 1 && nextAllowed.has(p.key);
          const canRetreat = i < currentIdx && allowedRetreat.has(p.key);
          const isClickable = isNext || canRetreat;

          let cls = styles.phase;
          if (isCurrent) cls += ` ${styles.current}`;
          if (isDone) cls += ` ${styles.done}`;
          if (isClickable) cls += ` ${styles.clickable}`;

          const phaseConditions = isCurrent ? conditions : [];

          return (
            <div
              key={p.key}
              className={cls}
              onClick={() => isClickable && handlePhaseClick(p.key)}
              title={
                isCurrent && phaseConditions.length > 0
                  ? phaseConditions
                      .map(
                        (c) =>
                          `${c.met ? '已满足' : '待完成'}: ${c.name}${
                            c.current !== undefined ? ` (${c.current}/${c.target})` : ''
                          }`,
                      )
                      .join('\n')
                  : undefined
              }
            >
              <span className={styles.dot}>{isDone ? 'ok' : isCurrent ? 'on' : '--'}</span>
              <div className={styles.phaseInfo}>
                <span className={styles.phaseLabel}>{p.label}</span>
                <span className={styles.phaseDesc}>{p.desc}</span>
              </div>
              {isCurrent && phaseConditions.length > 0 && (
                <div className={styles.conditionsMini}>
                  {phaseConditions.map((c, ci) => (
                    <div
                      key={ci}
                      className={`${styles.condDot} ${c.met ? styles.condMet : styles.condPending}`}
                      title={`${c.name}${
                        c.current !== undefined ? ` (${c.current}/${c.target})` : ''
                      }`}
                    />
                  ))}
                </div>
              )}
            </div>
          );
        })}
      </div>
      {state.storyOverview && (
        <div className={styles.tree}>
          {state.storyOverview.current_arc && (
            <div className={styles.treeItem}>
              <BookOpen size={12} aria-hidden="true" className={styles.treeIcon} />
              <span>{state.storyOverview.current_arc.title}</span>
            </div>
          )}
          {state.storyOverview.current_chapter && (
            <div className={styles.treeItem}>
              <FileText size={12} aria-hidden="true" className={styles.treeIcon} />
              <span>
                第 {state.storyOverview.current_chapter.number} 章：{' '}
                {state.storyOverview.current_chapter.title}
              </span>
              <span className={styles.badge}>
                {state.storyOverview.current_chapter.scene_count} 个场景
              </span>
            </div>
          )}
          {state.storyOverview.current_scene && (
            <div className={`${styles.treeItem} ${styles.treeScene}`}>
              <PenLine size={12} aria-hidden="true" className={styles.treeIcon} />
              <span>{state.storyOverview.current_scene.title}</span>
              <span className={styles.badge}>{state.storyOverview.current_scene.status}</span>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
