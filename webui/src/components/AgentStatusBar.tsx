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
import { useI18n } from '../i18n';
import styles from './AgentStatusBar.module.css';

const stageLabels: Array<{ id: StatusLabel; key: string; Icon: LucideIcon }> = [
  { id: 'thinking', key: 'status.stage.thinking', Icon: Brain },
  { id: 'acting', key: 'status.stage.acting', Icon: Wrench },
  { id: 'observing', key: 'status.stage.observing', Icon: Eye },
  { id: 'responding', key: 'status.stage.responding', Icon: MessageSquare },
];

interface Props {
  connectionState?: ConnectionState;
}

function formatContext(value: number, unit: string) {
  const label = value >= 1000 ? `${(value / 1000).toFixed(1)}K` : `${value}`;
  return `${label} ${unit}`;
}

function connectionLabel(value: ConnectionState | undefined, t: (key: string) => string) {
  if (!value) return t('status.connection.pending');
  return t(`status.connection.${value}`);
}

export default function AgentStatusBar({ connectionState }: Props) {
  const { t } = useI18n();
  const { state } = useAppState();
  const status = state.status;
  const active = state.currentRun !== null || status !== 'idle';
  const tokenTotal = state.usage.inputTokens + state.usage.outputTokens;
  const model = state.selectedModel || state.metadata?.model || t('status.defaultAssistant');
  const runtimeFeatures = [
    state.metadata?.memory?.enabled ? t('status.feature.memory') : null,
    state.metadata?.worldbuilding?.enabled ? t('status.feature.world') : null,
    state.metadata?.delegation_patterns?.length ? t('status.feature.collaboration') : null,
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
          <strong>{t(`status.${status}.title`)}</strong>
          <span>{t(`status.${status}.detail`)}</span>
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
              {t(stage.key)}
            </span>
          );
        })}
      </div>

      <div className={styles.meta}>
        <span>
          <Activity size={12} aria-hidden="true" />
          {connectionLabel(connectionState, t)}
        </span>
        <span>
          <Brain size={12} aria-hidden="true" />
          {model}
        </span>
        <span>
          <Clock3 size={12} aria-hidden="true" />
          {formatContext(tokenTotal, t('status.contextUnit'))}
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
