import { request } from './http';
import type { OkResponse, WorldFileLinkInput, WorldFileListResponse } from './types';

type WorldFileTarget = Required<Pick<WorldFileLinkInput, 'entity_type' | 'entity_id'>>;

const filesPath = (worldId: string) => `/api/worldbuilding/${encodeURIComponent(worldId)}/files`;

function fileTarget(link?: Pick<WorldFileLinkInput, 'entity_type' | 'entity_id'>) {
  if (!link?.entity_type || !link.entity_id) {
    throw new Error('File link target is required');
  }
  return { target_type: link.entity_type, target_id: link.entity_id };
}

export const filesApi = {
  listWorldFiles: async (worldId: string): Promise<WorldFileListResponse> => {
    const response = await request<WorldFileListResponse>('GET', filesPath(worldId));
    return { ...response, items: response.items ?? response.files };
  },

  linkWorldFile: (worldId: string, link: WorldFileLinkInput) =>
    request<OkResponse>('POST', filesPath(worldId), {
      file_path: link.file_path,
      ...fileTarget(link),
    }),

  unlinkWorldFile: async (worldId: string, filePath: string, target: WorldFileTarget) =>
    request<OkResponse>(
      'DELETE',
      `${filesPath(worldId)}/${encodeURIComponent(filePath)}`,
      fileTarget(target),
    ),
};
