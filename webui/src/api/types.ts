export interface SseFrame {
  seq: number;
  type: string;
  payload: Record<string, unknown>;
}

export type MessageKind = 'user' | 'assistant' | 'tool' | 'system' | 'approval' | 'status_pill';

export type StatusLabel = 'idle' | 'thinking' | 'acting' | 'observing' | 'waiting_approval';

export interface Message {
  id: string;
  kind: MessageKind;
  text?: string;
  toolCallId?: string;
  toolName?: string;
  toolArgs?: string;
  toolOutput?: string;
  toolIsError?: boolean;
  toolRunning?: boolean;
  approvalId?: string;
  approvalName?: string;
  approvalArgs?: string;
  statusLabel?: StatusLabel;
  error?: boolean;
}

export interface ModelEntry {
  name: string;
  provider: string;
  max_context_tokens: number;
}

export interface ToolSpec {
  name: string;
  description: string;
  source: string;
}

export interface AgentMetadata {
  id: string;
  description: string;
}

export interface McpServerStatus {
  name: string;
  alive: boolean;
}

export interface RuntimeMetadata {
  provider: string;
  model: string;
  models: ModelEntry[];
  permission_mode: string;
  memory: { enabled: boolean };
  tools: ToolSpec[];
  mcp_servers: McpServerStatus[];
  agents: AgentMetadata[];
  delegation_patterns: string[];
}

export interface SessionSummary {
  id: string;
  title: string;
  last_seq: number;
  created_at: string;
  updated_at: string;
  archived_at: string | null;
}

export interface WorldSummary {
  id: string;
  name: string;
  description: string;
  created_at: string;
}

export interface WorldAgent {
  id: string;
  name: string;
  display_name: string;
  kind: string;
}

export interface ForeshadowingItem {
  id: string;
  content: string;
  status?: string;
}

export interface SecretItem {
  id: string;
  title?: string;
  content?: string;
  status?: string;
}
