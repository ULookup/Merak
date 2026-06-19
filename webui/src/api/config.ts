import { request } from './http';
import type { LlmConfigFull, OkResponse, PreferencesResponse, UserPreferences } from './types';

export interface SaveLlmConfig {
  provider?: string;
  api_key?: string;
  api_base_url?: string;
  default_model?: string;
  max_output_tokens?: number;
  temperature?: number;
  context_memory_length?: 'short' | 'medium' | 'long';
  writer_model?: string;
}

export const configApi = {
  getConfig: () => request<LlmConfigFull>('GET', '/api/config/llm'),
  saveConfig: (config: SaveLlmConfig) => request<OkResponse>('POST', '/api/config/llm', config),
  testConfig: () => request<OkResponse>('POST', '/api/config/llm/test'),
  getPreferences: () => request<PreferencesResponse>('GET', '/api/config/preferences'),
  savePreferences: (preferences: Partial<UserPreferences>) =>
    request<OkResponse>('PUT', '/api/config/preferences', preferences),
};
