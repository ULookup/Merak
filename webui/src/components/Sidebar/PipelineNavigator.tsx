import { useAppState } from '../../AppState';
import type { CreativePhase } from '../../api/types';
import styles from './PipelineNavigator.module.css';

const PHASES: { key: CreativePhase; label: string; desc: string }[] = [
  { key: 'worldbuilding', label: '世界观构建', desc: '设定世界基础' },
  { key: 'character_creation', label: '角色创建', desc: '创建和完善角色' },
  { key: 'plot_architecture', label: '剧情架构', desc: '规划故事结构' },
  { key: 'scene_writing', label: '场景写作', desc: '创作具体场景' },
  { key: 'reflection', label: '回顾整理', desc: '检查与回顾' },
];

export default function PipelineNavigator() {
  const { state } = useAppState();
  const currentPhase: CreativePhase = (state.pipelinePhase ?? 'worldbuilding') as CreativePhase;
  const currentIdx = PHASES.findIndex(p => p.key === currentPhase);

  return (
    <div className={styles.container}>
      <div className={styles.title}>创作管线</div>
      <div className={styles.phases}>
        {PHASES.map((p, i) => {
          const isCurrent = i === currentIdx;
          const isDone = i < currentIdx;
          let cls = styles.phase;
          if (isCurrent) cls += ` ${styles.current}`;
          if (isDone) cls += ` ${styles.done}`;
          return (
            <div key={p.key} className={cls}>
              <span className={styles.dot}>
                {isDone ? '✓' : isCurrent ? '●' : '○'}
              </span>
              <div className={styles.phaseInfo}>
                <span className={styles.phaseLabel}>{p.label}</span>
                <span className={styles.phaseDesc}>{p.desc}</span>
              </div>
            </div>
          );
        })}
      </div>
      {state.storyOverview && (
        <div className={styles.tree}>
          {state.storyOverview.current_arc && (
            <div className={styles.treeItem}>
              <span className={styles.treeIcon}>📖</span>
              <span>{state.storyOverview.current_arc.title}</span>
            </div>
          )}
          {state.storyOverview.current_chapter && (
            <div className={styles.treeItem}>
              <span className={styles.treeIcon}>📄</span>
              <span>第{state.storyOverview.current_chapter.number}章 {state.storyOverview.current_chapter.title}</span>
              <span className={styles.badge}>{state.storyOverview.current_chapter.scene_count} 场景</span>
            </div>
          )}
          {state.storyOverview.current_scene && (
            <div className={`${styles.treeItem} ${styles.treeScene}`}>
              <span className={styles.treeIcon}>✎</span>
              <span>{state.storyOverview.current_scene.title}</span>
              <span className={styles.badge}>{state.storyOverview.current_scene.status}</span>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
