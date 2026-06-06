import {
  Activity,
  Brain,
  CheckCircle2,
  Clock3,
  Eye,
  MessageSquare,
  PauseCircle,
  Wrench,
} from 'lucide-react';
import type { LucideIcon } from 'lucide-react';
import type { StatusLabel } from '../api/types';
import { useAppState } from '../AppState';
import type { ConnectionState } from '../hooks/useSSE';
import styles from './AgentStatusBar.module.css';

const stageLabels: Array<{ id: StatusLabel; label: string; Icon: LucideIcon }> = [
  { id: 'thinking', label: 'Thinking', Icon: Brain },
  { id: 'acting', label: 'Acting', Icon: Wrench },
  { id: 'observing', label: 'Observing', Icon: Eye },
  { id: 'responding', label: 'Responding', Icon: MessageSquare },
];

const copy: Record<StatusLabel, { title: string; detail: string }> = {
  idle: {
    title: 'Ready',
    detail: 'Start a run to stream reasoning, tools, and Markdown output.',
  },
  thinking: {
    title: 'Thinking',
    detail: 'The agent is planning before text or tool output appears.',
  },
  acting: {
    title: 'Acting',
    detail: 'A tool call or delegated step is in progress.',
  },
  observing: {
    title: 'Observing',
    detail: 'Tool results are being read back into the run.',
  },
  responding: {
    title: 'Responding',
    detail: 'Markdown output is streaming into the conversation.',
  },
  waiting_approval: {
    title: 'Waiting Approval',
    detail: 'A requested tool action needs a decision before the run continues.',
  },
};

interface Props {
  connectionState?: ConnectionState;
}

function formatTokens(value: number) {
  if (value >= 1000) return `${(value / 1000).toFixed(1)}K tokens`;
  return `${value} tokens`;
}

function connectionLabel(value: ConnectionState | undefined) {
  if (value === 'connected') return 'SSE connected';
  return value ? `SSE ${value}` : 'SSE pending';
}

export default function AgentStatusBar({ connectionState }: Props) {
  const { state } = useAppState();
  const status = state.status;
  const active = state.currentRun !== null || status !== 'idle';
  const current = copy[status] ?? copy.idle;
  const tokenTotal = state.usage.inputTokens + state.usage.outputTokens;
  const model = state.selectedModel || state.metadata?.model || 'Default model';
  const runtimeFeatures = [
    state.metadata?.memory?.enabled ? 'memory' : null,
    state.metadata?.worldbuilding?.enabled ? 'world' : null,
    state.metadata?.delegation_patterns?.length ? 'delegation' : null,
  ].filter(Boolean);

  return (
    <section
      className={`${styles.bar} ${active ? styles.barActive : ''}`}
      aria-label="Agent run status"
      data-testid="agent-status-bar"
    >
      <div className={styles.summary}>
        <span className={styles.orbit} aria-hidden="true">
          {status === 'idle' ? (
            <CheckCircle2 size={16} strokeWidth={2.4} />
          ) : status === 'waiting_approval' ? (
            <PauseCircle size={16} strokeWidth={2.4} />
          ) : (
            <Activity size={16} strokeWidth={2.4} />
          )}
        </span>
        <div className={styles.statusCopy}>
          <strong>{current.title}</strong>
          <span>{current.detail}</span>
        </div>
      </div>

      <div className={styles.stages} aria-label="Run stages">
        {stageLabels.map((stage) => {
          const selected = stage.id === status;
          const { Icon } = stage;
          return (
            <span
              key={stage.id}
              className={`${styles.stage} ${selected ? styles.stageActive : ''}`}
              aria-current={selected ? 'step' : undefined}
            >
              <Icon size={13} aria-hidden="true" strokeWidth={2.3} />
              {stage.label}
            </span>
          );
        })}
      </div>

      <div className={styles.meta}>
        <span>
          <Activity size={12} aria-hidden="true" />
          {connectionLabel(connectionState)}
        </span>
        <span>
          <Brain size={12} aria-hidden="true" />
          {model}
        </span>
        <span>
          <Clock3 size={12} aria-hidden="true" />
          {formatTokens(tokenTotal)}
        </span>
        {runtimeFeatures.length > 0 && (
          <span>
            <Wrench size={12} aria-hidden="true" />
            {runtimeFeatures.join(' / ')}
          </span>
        )}
      </div>
    </section>
  );
}
