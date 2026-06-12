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
  assistantStatus?: StatusLabel;
  thinkingStartedAt?: number;
  thinkingCompletedAt?: number;
  pending?: boolean;
  toolCallId?: string;
  toolName?: string;
  toolArgs?: string;
  toolOutput?: string;
  toolIsError?: boolean;
  toolRunning?: boolean;
  approvalId?: string;
  approvalName?: string;
  approvalArgs?: string;
  approvalResolved?: boolean;
  approvalDecision?: string;
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
  world_id: string | null;
  agent_id: string | null;
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
  updated_at?: string;
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
  avatar_url?: string;
}

export interface AgentListResponse {
  ok: boolean;
  agents: WorldAgent[];
}

export interface ForeshadowingItem {
  id: string;
  content: string;
  status?: string;
  pay_off_idea?: string;
  hint_level?: string;
  tags?: string[];
  planted_at?: string;
  paid_at?: string;
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
  truth?: string;
  public_version?: string;
  stakes?: string;
  status?: string;
  aware_character_ids?: string[];
  suspicious_character_ids?: string[];
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

export interface UiCapabilities {
  files: boolean;
  story_overview: boolean;
  session_archive: boolean;
  world_create: boolean;
  editor_save: boolean;
}

export interface CapabilitiesResponse {
  ok: boolean;
  capabilities: UiCapabilities;
  fallback?: boolean;
}

export interface WorldDetail {
  id: string;
  name: string;
  description: string;
  created_at: string;
  updated_at?: string;
  stats: {
    agents: number;
    chapters: number;
    scenes: number;
    open_foreshadowing: number;
    active_secrets: number;
  };
}

export interface WorldDetailResponse {
  ok: boolean;
  world: WorldDetail;
  fallback?: boolean;
}

export interface StoryChapter {
  id: string;
  title: string;
  number: number;
  status: string;
  arc_id?: string;
  scene_count: number;
  updated_at: string;
}

export interface StoryScene {
  id: string;
  title: string;
  chapter_id: string;
  world_time: string;
  status: string;
  participant_ids: string[];
  updated_at: string;
}

export interface StoryOverview {
  current_arc?: {
    id: string;
    title: string;
    status: string;
    purpose?: string;
  };
  current_chapter?: StoryChapter;
  current_scene?: StoryScene;
  agents: WorldAgent[];
  foreshadowing: ForeshadowingItem[];
  secrets: SecretItem[];
  world_time: string | null;
  fallback?: boolean;
}

export interface StoryOverviewResponse {
  ok: boolean;
  overview: StoryOverview;
  fallback?: boolean;
}

export interface ChapterListResponse {
  ok: boolean;
  chapters: StoryChapter[];
  fallback?: boolean;
}

export interface SceneListResponse {
  ok: boolean;
  scenes: StoryScene[];
  fallback?: boolean;
}

export interface WorkspaceFile {
  id: string;
  path: string;
  name: string;
  ext: string;
  mime: string;
  size: number;
  updated_at: string;
  generated_by_run_id?: string;
  dirty: boolean;
  fallback?: boolean;
}

export interface WorkspaceFileListResponse {
  ok: boolean;
  root: string;
  files: WorkspaceFile[];
  fallback?: boolean;
}

export interface WorkspaceFileContent {
  path: string;
  content: string;
  encoding: 'utf-8';
  updated_at: string;
  version: string;
  fallback?: boolean;
}

export interface WorkspaceFileContentResponse {
  ok: boolean;
  file: WorkspaceFileContent;
  fallback?: boolean;
}

export type AgentImageType = 'avatar' | 'design';

export interface AgentImage {
  id: string;
  agent_id: string;
  image_type: AgentImageType;
  mime_type: string;
  original_name: string;
  file_size_bytes: number;
  is_primary: boolean;
  sort_order: number;
  created_at: string;
  url: string;
}

export interface AgentImageGroups {
  avatar: AgentImage[];
  design: AgentImage[];
}

export interface AgentImageListResponse {
  ok: boolean;
  images: AgentImage[];
}

export interface AgentImageUploadResponse {
  ok: boolean;
  image: AgentImage;
}

export interface ChunkedImageInitResponse {
  ok: boolean;
  upload_id: string;
  chunks_total: number;
  chunk_size: number;
}

export interface ChunkedImageProgressResponse {
  ok: boolean;
  upload_id: string;
  chunks_total: number;
  chunks_uploaded: number[];
  chunk_size: number;
  total_size: number;
}

export interface SaveWorkspaceFileResponse {
  ok: boolean;
  file: Pick<WorkspaceFileContent, 'path' | 'updated_at' | 'version'>;
  fallback?: boolean;
}

export interface ArchiveSessionResponse {
  ok: boolean;
  session: SessionSummary;
  fallback?: boolean;
}

export interface RunTimelineToolCall {
  id: string;
  name: string;
  status: string;
  started_at?: string;
  completed_at?: string;
}

export interface RunDetail {
  id: string;
  session_id: string;
  status: string;
  model: string;
  started_at: string;
  completed_at?: string;
  input_tokens: number;
  output_tokens: number;
  tool_calls: RunTimelineToolCall[];
}

export interface RunDetailResponse {
  ok: boolean;
  run: RunDetail;
  fallback?: boolean;
}

// === Card API ===
export interface CharacterCardDetail {
  version: number;
  age?: number;
  gender?: string;
  race?: string;
  identity?: string;
  core_traits: string[];
  emotional_tendency?: string;
  speaking_style?: string;
  core_desire?: string;
  deep_fear?: string;
  daily_goal?: string;
  background?: string;
  knowledge_scope?: string;
  appearance?: string;
  taboo_topics?: string[];
}

export interface AgentDetail {
  id: string;
  world_id: string;
  name: string;
  display_name: string;
  kind: string;
  created_at: string;
  updated_at: string;
  avatar_url?: string;
  images?: AgentImageGroups;
  character_card: CharacterCardDetail;
}

export interface AgentDetailResponse {
  ok: boolean;
  agent: AgentDetail;
}

export interface PatchAgentCardResponse {
  ok: boolean;
  version: number;
}

export interface DiaryEntry {
  id: string;
  agent_id: string;
  scene_id?: string;
  content: string;
  world_time?: string;
  created_at: string;
}

export interface DiaryListResponse {
  ok: boolean;
  diaries: DiaryEntry[];
}

export interface RelationEntry {
  agent_id: string;
  target_id: string;
  relation_type: string;
  description?: string;
  intimacy?: number;
  key_events?: string[];
  updated_at: string;
}

export interface RelationListResponse {
  ok: boolean;
  relations: RelationEntry[];
}

// === Pipeline ===
export type CreativePhase =
  | 'worldbuilding'
  | 'character_creation'
  | 'plot_architecture'
  | 'scene_writing'
  | 'reflection';

export interface PipelineState {
  world_id: string;
  current_phase: CreativePhase;
  active_arc_id?: string;
  active_chapter_id?: string;
  active_scene_id?: string;
  scene_count_in_chapter: number;
  total_scenes_target: number;
  needs_diary_update: boolean;
  needs_character_update: boolean;
}

// === Pipeline (extended) ===

export interface ConditionState {
  name: string;
  met: boolean;
  current?: number;
  target?: number;
}

export interface PhaseTransition {
  id: string;
  from: CreativePhase;
  to: CreativePhase;
  trigger: string;
  timestamp: string;
  world_id?: string;
}

export interface PipelineHistoryResponse {
  ok: boolean;
  history: PhaseTransition[];
}

export interface PipelineViewData {
  phase: CreativePhase;
  label: string;
  conditions: ConditionState[];
  all_conditions_met: boolean;
  active_workflow: string;
  recent_history: PhaseTransition[];
  next_allowed: CreativePhase[];
  allowed_retreat: CreativePhase[];
}

export interface WorkflowSummary {
  name: string;
  description: string;
  version: number;
  phase_count: number;
}

// === Review ===
export interface ReviewIssue {
  severity: 'error' | 'warning' | 'info';
  category: 'consistency' | 'pacing' | 'foreshadow' | 'character' | 'style';
  location: string;
  description: string;
  suggestion: string;
}

export interface ReviewSummary {
  pass: boolean;
  issue_count: number;
  suggestion: string;
}

// === Audit ===
export interface RunAudit {
  run_id: string;
  status: string;
  duration_seconds: number;
  turn_count: number;
  tool_call_stats: Record<string, number>;
  token_usage: { input: number; output: number };
  approval_count: number;
  checkpoint_count: number;
}

export interface RunAuditResponse {
  ok: boolean;
  audit: RunAudit;
}

// === Creation mutations ===
export interface CreateSceneResponse {
  ok: boolean;
  scene_id: string;
}

export interface EndSceneResponse {
  ok: boolean;
  diaries_written: { id: string; agent_id: string; scene_id?: string }[];
  diary_count: number;
  relations_updated: number;
  proposed_foreshadowing: { id: string; content: string }[];
  leak_risks: number;
}

export interface CreateForeshadowingResponse {
  ok: boolean;
  item: ForeshadowingItem;
  foreshadowing_id: string;
}

export interface CreateSecretResponse {
  ok: boolean;
  item: SecretItem;
  secret_id: string;
}

export interface CreateAgentResponse {
  ok: boolean;
  agent_id: string;
  name: string;
}

export interface AgentPromptResponse {
  ok: boolean;
  agent_id: string;
  prompt: string;
}

export interface ResolveCreationResponse {
  ok: boolean;
  status: string;
}
