export interface SseFrame {
  seq: number;
  type: string;
  payload: Record<string, unknown>;
}

export type MessageKind = 'user' | 'assistant' | 'tool' | 'system' | 'approval' | 'status_pill';

export type StatusLabel =
  | 'idle'
  | 'thinking'
  | 'responding'
  | 'acting'
  | 'observing'
  | 'waiting_approval';

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
  worldbuilding?: { enabled: boolean };
  tui?: { theme: string };
  tools: ToolSpec[];
  mcp_servers: McpServerStatus[];
  agents: AgentMetadata[];
  delegation_patterns: string[];
}

export interface CreateSessionResponse {
  session_id: string;
  session?: SessionSummary;
}

export interface SessionListResponse {
  sessions: SessionSummary[];
}

export interface UpdateSessionResponse {
  session: SessionSummary;
}

export interface GenerateTitleResponse {
  title: string;
}

export interface StartRunResponse {
  run_id: string;
  session_id: string;
  model: string;
}

export interface CancelRunResponse {
  run_id: string;
  status: string;
}

export interface ApprovalResponse {
  approval_id: string;
  status: string;
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

export interface WorldListResponse {
  ok: boolean;
  worlds: WorldSummary[];
}

export interface UpdateWorldResponse {
  ok: boolean;
  world_id: string;
  name: string;
  description: string;
}

export interface WorldAgent {
  id: string;
  name: string;
  display_name: string;
  kind: string;
}

export interface AgentListResponse {
  ok: boolean;
  agents: WorldAgent[];
}

export interface ForeshadowingItem {
  id: string;
  content: string;
  status?: string;
}

export interface ForeshadowingListResponse {
  ok: boolean;
  foreshadowing?: ForeshadowingItem[];
  items?: ForeshadowingItem[];
}

export interface SecretItem {
  id: string;
  title?: string;
  content?: string;
  status?: string;
}

export interface SecretListResponse {
  ok: boolean;
  secrets?: SecretItem[];
  items?: SecretItem[];
}

export interface WorldTimeResponse {
  ok: boolean;
  day?: number;
  period?: number;
  label?: string;
  time?: string;
  world_time?: string;
  now?: string;
}

export interface LlmConfig {
  provider: string;
  api_key_masked: string;
  api_base_url: string;
  default_model: string;
  max_output_tokens: number;
}

export interface OkResponse {
  ok: boolean;
  message?: string;
}

export interface OpenWorkspacePathResponse {
  ok: boolean;
  path: string;
}
