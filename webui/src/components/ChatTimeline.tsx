import { useEffect, useRef } from 'react';
import type { Message } from '../api/types';
import { useAppState } from '../AppState';
import type { ConnectionState } from '../hooks/useSSE';
import AgentStatusBar from './AgentStatusBar';
import ApprovalCell from './cells/ApprovalCell';
import AssistantCell from './cells/AssistantCell';
import StatusPill from './cells/StatusPill';
import SystemCell from './cells/SystemCell';
import ToolCell from './cells/ToolCell';
import UserCell from './cells/UserCell';
import styles from './ChatTimeline.module.css';

function renderMessage(msg: Message) {
  switch (msg.kind) {
    case 'user':
      return <UserCell key={msg.id} text={msg.text ?? ''} />;
    case 'assistant':
      return (
        <AssistantCell
          key={msg.id}
          text={msg.text ?? ''}
          status={msg.assistantStatus}
          pending={msg.pending}
          thinkingStartedAt={msg.thinkingStartedAt}
          thinkingCompletedAt={msg.thinkingCompletedAt}
        />
      );
    case 'tool':
      return (
        <ToolCell
          key={msg.id}
          toolName={msg.toolName ?? ''}
          toolArgs={msg.toolArgs}
          toolOutput={msg.toolOutput}
          toolRunning={msg.toolRunning}
          toolIsError={msg.toolIsError}
        />
      );
    case 'approval':
      return (
        <ApprovalCell
          key={msg.id}
          approvalId={msg.approvalId ?? ''}
          approvalName={msg.approvalName ?? ''}
          approvalArgs={msg.approvalArgs}
          approvalResolved={msg.approvalResolved}
          approvalDecision={msg.approvalDecision}
        />
      );
    case 'system':
      return <SystemCell key={msg.id} text={msg.text ?? ''} error={msg.error} />;
    case 'status_pill':
      return <StatusPill key={msg.id} label={msg.statusLabel ?? 'idle'} />;
    default:
      return null;
  }
}

interface ChatTimelineProps {
  connectionState?: ConnectionState;
}

export default function ChatTimeline({ connectionState }: ChatTimelineProps) {
  const { state } = useAppState();
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView?.({ behavior: 'smooth' });
  }, [state.messages]);

  return (
    <>
      <AgentStatusBar connectionState={connectionState} />
      <div className={styles.area} role="log" aria-live="polite" aria-label="Messages">
        {state.messages.length === 0 && (
          <div className={styles.emptyState}>
            <div className={styles.emptyMark} role="img" aria-label="Scene ready mark">
              <svg viewBox="0 0 80 70" aria-hidden="true">
                <ellipse cx="42" cy="35" rx="27" ry="12" transform="rotate(-18 42 35)" />
                <path d="M20 56L33.5 23.5C34.4 21.3 37.4 21 38.8 22.9L46 32.5C47.2 34 46.7 36.3 45 37.3L20 56Z" />
                <circle cx="45" cy="33" r="16" />
                <path d="M38 19C45.4 21.2 51 26.9 52.8 34.5C47.9 32.5 42.6 31.8 37.4 32.4C38.6 28 38.8 23.6 38 19Z" />
                <circle cx="64" cy="15" r="4" />
              </svg>
            </div>
            <h2>Build the next scene</h2>
            <p>
              Start with a character voice, a world rule, or a scene beat. Runs stream through SSE
              as Markdown, tools, approvals, and delegation events.
            </p>
          </div>
        )}
        {state.messages.map(renderMessage)}
        <div ref={bottomRef} />
      </div>
    </>
  );
}
