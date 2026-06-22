import { request } from './http';
import type {
  FactionItem,
  FactionListResponse,
  GraphEntity,
  GraphEntityListResponse,
  KnowledgeListResponse,
  LocationListResponse,
  OkResponse,
  ResourceListResponse,
  TimelineResponse,
} from './types';

function adaptList<T, TResponse extends ResourceListResponse<T>>(
  response: TResponse,
  namedItems?: T[],
): TResponse {
  return {
    ...response,
    items: response.items ?? namedItems,
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
    request<LocationListResponse>('GET', resourcePath(worldId, 'locations')).then((response) =>
      adaptList(response, response.locations),
    ),

  listKnowledge: async (worldId: string) =>
    request<KnowledgeListResponse>('GET', resourcePath(worldId, 'knowledge')).then((response) =>
      adaptList(response, response.knowledge),
    ),

  listFactions: async (worldId: string) =>
    request<FactionListResponse>('GET', resourcePath(worldId, 'factions')).then((response) =>
      adaptList<FactionItem, FactionListResponse>(response, response.factions),
    ),

  getTimeline: async (worldId: string) =>
    request<TimelineResponse>('GET', resourcePath(worldId, 'timeline')).then((response) =>
      adaptList(response, response.events),
    ),

  listGraphEntities: async (worldId: string) =>
    request<GraphEntityListResponse>('GET', resourcePath(worldId, 'knowledge-graph/entities')).then(
      (response) => adaptList<GraphEntity, GraphEntityListResponse>(response, response.entities),
    ),

  reorderChapters: (worldId: string, chapterIds: string[]) =>
    request<OkResponse>('POST', resourcePath(worldId, 'chapters/reorder'), {
      order: chapterIds,
    }),

  deleteLocation: (worldId: string, locationId: string) =>
    request<OkResponse>('DELETE', resourcePath(worldId, 'locations', locationId)),

  deleteKnowledge: (worldId: string, knowledgeId: string) =>
    request<OkResponse>('DELETE', resourcePath(worldId, 'knowledge', knowledgeId)),

  deleteFaction: (worldId: string, factionId: string) =>
    request<OkResponse>('DELETE', resourcePath(worldId, 'factions', factionId)),
};
