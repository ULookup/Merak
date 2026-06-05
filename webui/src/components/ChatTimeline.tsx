import { useEffect, useRef } from 'react';
import { useAppState } from '../AppState';
import type { Message } from '../api/types';
import UserCell from './cells/UserCell';
import AssistantCell from './cells/AssistantCell';
import ToolCell from './cells/ToolCell';
import ApprovalCell from './cells/ApprovalCell';
import SystemCell from './cells/SystemCell';
import StatusPill from './cells/StatusPill';

function renderMessage(msg: Message) {
  switch (msg.kind) {
    case 'user':
      return <UserCell key={msg.id} text={msg.text ?? ''} />;
    case 'assistant':
      return <AssistantCell key={msg.id} text={msg.text ?? ''} />;
    case 'tool':
      return (
        <ToolCell key={msg.id}
          toolName={msg.toolName ?? ''}
          toolArgs={msg.toolArgs}
          toolOutput={msg.toolOutput}
          toolRunning={msg.toolRunning}
          toolIsError={msg.toolIsError}
        />
      );
    case 'approval':
      return (
        <ApprovalCell key={msg.id}
          approvalId={msg.approvalId ?? ''}
          approvalName={msg.approvalName ?? ''}
          approvalArgs={msg.approvalArgs}
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

export default function ChatTimeline() {
  const { state } = useAppState();
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [state.messages]);

  return (
    <div className="chat-area">
      {state.messages.map(renderMessage)}
      <div ref={bottomRef} />
    </div>
  );
}
