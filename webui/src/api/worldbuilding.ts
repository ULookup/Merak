import { request } from './http';
import type {
  FactionItem,
  GraphEntity,
  KnowledgeItem,
  LocationItem,
  OkResponse,
  ResourceListResponse,
  TimelineEvent,
} from './types';

type NamedListResponse<T> = ResourceListResponse<T> & Record<string, unknown>;

function adaptList<T>(response: NamedListResponse<T>, key: string): ResourceListResponse<T> {
  const namedItems = response[key];
  return {
    ...response,
    items: response.items ?? (Array.isArray(namedItems) ? (namedItems as T[]) : undefined),
  };
}

const worldPath = (worldId: string) => `/api/worldbuilding/${encodeURIComponent(worldId)}`;
const resourcePath = (worldId: string, resource: string, resourceId?: string) =>
  `${worldPath(worldId)}/${resource}${resourceId ? `/${encodeURIComponent(resourceId)}` : ''}`;

export const worldbuildingApi = {
  getDashboard: (worldId: string) =>
    request<{ ok?: boolean; dashboard?: Record<string, unknown> }>(
      'POST',
      `${worldPath(worldId)}/dashboard`,
    ),

  listLocations: async (worldId: string) =>
    adaptList<LocationItem>(
      await request<NamedListResponse<LocationItem>>('GET', resourcePath(worldId, 'locations')),
      'locations',
    ),

  listKnowledge: async (worldId: string) =>
    adaptList<KnowledgeItem>(
      await request<NamedListResponse<KnowledgeItem>>('GET', resourcePath(worldId, 'knowledge')),
      'knowledge',
    ),

  listFactions: async (worldId: string) =>
    adaptList<FactionItem>(
      await request<NamedListResponse<FactionItem>>('GET', resourcePath(worldId, 'factions')),
      'factions',
    ),

  getTimeline: async (worldId: string) =>
    adaptList<TimelineEvent>(
      await request<NamedListResponse<TimelineEvent>>('GET', resourcePath(worldId, 'timeline')),
      'events',
    ),

  listGraphEntities: async (worldId: string) =>
    adaptList<GraphEntity>(
      await request<NamedListResponse<GraphEntity>>(
        'GET',
        resourcePath(worldId, 'knowledge-graph/entities'),
      ),
      'entities',
    ),

  reorderChapters: (worldId: string, chapterIds: string[]) =>
    request<OkResponse>('POST', resourcePath(worldId, 'chapters/reorder'), {
      chapter_ids: chapterIds,
    }),

  deleteLocation: (worldId: string, locationId: string) =>
    request<OkResponse>('DELETE', resourcePath(worldId, 'locations', locationId)),

  deleteKnowledge: (worldId: string, knowledgeId: string) =>
    request<OkResponse>('DELETE', resourcePath(worldId, 'knowledge', knowledgeId)),

  deleteFaction: (worldId: string, factionId: string) =>
    request<OkResponse>('DELETE', resourcePath(worldId, 'factions', factionId)),
};
