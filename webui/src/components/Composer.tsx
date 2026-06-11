import { useCallback, useRef, useState } from 'react';
import {
  BookOpen,
  Feather,
  GitBranch,
  Globe2,
  PenLine,
  Send,
  Square,
  UsersRound,
  WandSparkles,
} from 'lucide-react';
import type { LucideIcon } from 'lucide-react';
import { api, formatApiError } from '../api/client';
import { useAppState } from '../AppState';
import styles from './Composer.module.css';
import { useToast } from './Toast';

const promptModes: Array<{ label: string; Icon: LucideIcon; template: string }> = [
  {
    label: 'Scene',
    Icon: PenLine,
    template:
      'Draft the next scene.\n\nScene goal:\nCharacters present:\nConflict:\nWorld constraints:\nDesired ending beat:',
  },
  {
    label: 'Character',
    Icon: Feather,
    template:
      'Develop a character voice card.\n\nName:\nRole in story:\nCore desire:\nDeep fear:\nSpeech pattern:\nKnowledge boundary:',
  },
  {
    label: 'World Rule',
    Icon: Globe2,
    template:
      'Add or refine a world rule.\n\nRule:\nWhy it matters:\nWho knows it:\nExceptions:\nScenes affected:',
  },
  {
    label: 'Outline',
    Icon: BookOpen,
    template:
      'Outline the next chapter.\n\nChapter purpose:\nOpening state:\nMajor turns:\nForeshadowing to plant/pay:\nClosing hook:',
  },
  {
    label: 'Rewrite',
    Icon: WandSparkles,
    template:
      'Rewrite the selected draft.\n\nKeep:\nChange:\nTone:\nContinuity constraints:\nTarget length:',
  },
];

export default function Composer() {
  const { state, dispatch } = useAppState();
  const { showToast } = useToast();

  const currentAgent = state.agents.find((a) => a.id === state.agentId);

  const placeholder = (() => {
    if (!currentAgent) return 'Send a message...';
    const kind = currentAgent.kind;
    if (kind === 'god' || kind === '0') return 'Send instructions as God...';
    if (kind === 'individual' || kind === 'group' || kind === '5' || kind === '6')
      return `Speak as ${currentAgent.display_name || currentAgent.name}...`;
    if (kind && (kind.includes('manager') || (kind >= '1' && kind <= '4')))
      return `Query as ${currentAgent.display_name || currentAgent.name}...`;
    return 'Send a message...';
  })();
  const [text, setText] = useState('');
  const [sending, setSending] = useState(false);
  const [runMode, setRunMode] = useState<'single' | 'delegate'>('single');
  const [delegationPattern, setDelegationPattern] = useState('fan_out');
  const [aggregation, setAggregation] = useState('all_results');
  const [selectedAgentIds, setSelectedAgentIds] = useState<string[]>([]);
  const ref = useRef<HTMLTextAreaElement>(null);

  const availablePatterns = state.metadata?.delegation_patterns?.length
    ? state.metadata.delegation_patterns
    : ['fan_out', 'sequential', 'pipeline'];

  function toggleAgent(agentId: string) {
    setSelectedAgentIds((current) =>
      current.includes(agentId)
        ? current.filter((id) => id !== agentId)
        : [...current, agentId],
    );
  }

  const cancel = useCallback(async () => {
    if (!state.currentRun) return;
    try {
      await api.cancelRun(state.currentRun);
    } catch {
      /* ignore */
    }
  }, [state.currentRun]);

  const send = useCallback(async () => {
    const msg = text.trim();
    if (!msg || sending) return;
    if (!state.sessionId) {
      showToast('Waiting for session...', 'info');
      return;
    }
    setText('');
    setSending(true);

    try {
      if (runMode === 'delegate') {
        const agents = selectedAgentIds.length
          ? selectedAgentIds
          : state.agentId
            ? [state.agentId]
            : [];
        if (agents.length === 0) {
          showToast('请选择至少一个 Agent。', 'info');
          setText(msg);
          return;
        }
        const res = await api.startDelegation(
          state.sessionId,
          delegationPattern,
          agents,
          msg,
          aggregation,
        );
        dispatch({ type: 'SET_CURRENT_RUN', runId: res.parent_run_id });
        dispatch({
          type: 'APPEND_MESSAGE',
          message: {
            id: `delegation_${Date.now()}`,
            kind: 'system',
            text: `Delegate 已启动：${delegationPattern} · ${agents.length} Agent`,
          },
        });
        showToast('Delegate Run 已启动。', 'success');
      } else {
        await api.startRun(state.sessionId, msg, state.selectedModel);
      }
    } catch (e) {
      showToast(formatApiError(e), 'error');
    } finally {
      setSending(false);
    }
  }, [
    aggregation,
    delegationPattern,
    dispatch,
    runMode,
    selectedAgentIds,
    sending,
    showToast,
    state.agentId,
    state.sessionId,
    state.selectedModel,
    text,
  ]);

  const isRunning =
    state.currentRun !== null && state.status !== 'idle' && state.status !== 'waiting_approval';

  function onKeyDown(e: React.KeyboardEvent) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      send();
    }
  }

  function insertMode(template: string) {
    setText((current) => (current.trim() ? `${current.trim()}\n\n${template}` : template));
    window.setTimeout(() => ref.current?.focus(), 0);
  }

  return (
    <div className={styles.area}>
      <div className={styles.modeRail} aria-label="Creative prompt modes">
        <div className={styles.runModeToggle} role="group" aria-label="Run mode">
          <button
            type="button"
            className={runMode === 'single' ? styles.runModeActive : styles.runModeBtn}
            onClick={() => setRunMode('single')}
            disabled={state.status !== 'idle' && state.status !== 'waiting_approval'}
          >
            <Send size={13} aria-hidden="true" strokeWidth={2.3} />
            Single
          </button>
          <button
            type="button"
            className={runMode === 'delegate' ? styles.runModeActive : styles.runModeBtn}
            onClick={() => setRunMode('delegate')}
            disabled={state.status !== 'idle' && state.status !== 'waiting_approval'}
          >
            <UsersRound size={13} aria-hidden="true" strokeWidth={2.3} />
            Delegate
          </button>
        </div>
        {promptModes.map(({ label, Icon, template }) => (
          <button
            key={label}
            type="button"
            className={styles.modeBtn}
            onClick={() => insertMode(template)}
            disabled={state.status !== 'idle' && state.status !== 'waiting_approval'}
            title={`Insert ${label} prompt`}
          >
            <Icon size={14} aria-hidden="true" strokeWidth={2.3} />
            {label}
          </button>
        ))}
      </div>
      {runMode === 'delegate' && (
        <div className={styles.delegatePanel} aria-label="Delegate run options">
          <div className={styles.delegateGroup}>
            <span>Pattern</span>
            <div className={styles.choiceRow}>
              {availablePatterns.map((pattern) => (
                <button
                  key={pattern}
                  type="button"
                  className={delegationPattern === pattern ? styles.choiceActive : styles.choiceBtn}
                  onClick={() => setDelegationPattern(pattern)}
                >
                  <GitBranch size={12} aria-hidden="true" strokeWidth={2.2} />
                  {pattern}
                </button>
              ))}
            </div>
          </div>
          <label className={styles.delegateField}>
            <span>Aggregation</span>
            <select value={aggregation} onChange={(e) => setAggregation(e.target.value)}>
              <option value="all_results">all_results</option>
              <option value="consensus">consensus</option>
              <option value="summary">summary</option>
            </select>
          </label>
          <div className={styles.delegateGroup}>
            <span>Agents</span>
            <div className={styles.agentChoices}>
              {state.agents.length === 0 ? (
                <span className={styles.delegateHint}>当前世界没有可选 Agent。</span>
              ) : (
                state.agents.map((agent) => (
                  <label key={agent.id} className={styles.agentChoice}>
                    <input
                      type="checkbox"
                      checked={selectedAgentIds.includes(agent.id)}
                      onChange={() => toggleAgent(agent.id)}
                    />
                    {agent.display_name || agent.name || agent.id}
                  </label>
                ))
              )}
            </div>
          </div>
        </div>
      )}
      <div className={styles.box}>
        <textarea
          ref={ref}
          className={styles.input}
          data-testid="composer-input"
          aria-label="Type a message"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={onKeyDown}
          placeholder={placeholder}
          rows={2}
          disabled={state.status !== 'idle' && state.status !== 'waiting_approval'}
        />
        {isRunning ? (
          <button
            className={styles.cancelBtn}
            onClick={cancel}
            data-testid="cancel-btn"
            aria-label="Cancel run"
          >
            <Square size={14} aria-hidden="true" strokeWidth={2.4} />
            Cancel
          </button>
        ) : (
          <button
            className={styles.sendBtn}
            onClick={send}
            disabled={sending || !text.trim()}
            data-testid="send-btn"
            aria-label="Send message"
          >
            <Send size={14} aria-hidden="true" strokeWidth={2.4} />
            {runMode === 'delegate' ? 'Start Delegate' : 'Send'}
          </button>
        )}
      </div>
      <div className={styles.hint}>
        Enter &middot; send &nbsp;|&nbsp; Shift+Enter &middot; newline
      </div>
    </div>
  );
}
