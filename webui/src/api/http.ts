let apiBase = import.meta.env.VITE_API_BASE ?? '';

export function setApiBase(base: string) {
  apiBase = base.replace(/\/$/, '');
}

export function getApiBase() {
  return apiBase;
}

export function apiUrl(path: string) {
  if (/^https?:\/\//i.test(path)) return path;
  return `${apiBase}${path.startsWith('/') ? path : `/${path}`}`;
}

export class ApiError extends Error {
  constructor(
    message: string,
    public status: number,
    public code?: string,
    public retryable = false,
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

async function parseJsonResponse<T>(response: Response): Promise<T> {
  const payload = await response.json().catch(() => ({}));
  if (response.status >= 400) {
    const raw = payload?.error;
    const detail = typeof raw === 'object' && raw ? raw : {};
    throw new ApiError(
      detail.message ?? (typeof raw === 'string' ? raw : `Request failed (${response.status})`),
      response.status,
      detail.code,
      Boolean(detail.retryable),
    );
  }
  return payload as T;
}

export async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const response = await fetch(apiUrl(path), {
    method,
    headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  return parseJsonResponse<T>(response);
}

export async function requestForm<T>(path: string, form: FormData): Promise<T> {
  const response = await fetch(apiUrl(path), { method: 'POST', body: form });
  return parseJsonResponse<T>(response);
}

export async function requestBlob<T>(method: string, path: string, body?: Blob): Promise<T> {
  const response = await fetch(apiUrl(path), { method, body });
  return parseJsonResponse<T>(response);
}

export async function fallbackRequest<T>(path: string, fallback: T): Promise<T> {
  try {
    return await request<T>('GET', path);
  } catch {
    return fallback;
  }
}

export function formatApiError(
  error: unknown,
  fallback = '操作失败，请稍后重试。',
  t?: (key: string) => string,
) {
  if (error instanceof ApiError) {
    if (t && error.code) {
      const key = `error.${error.code}`;
      const translated = t(key);
      if (translated !== key) return translated;
    }
    switch (error.code) {
      case 'version_conflict':
        return '内容已在后端更新，请刷新后再保存。';
      case 'file_conflict':
        return '文件已被其他操作修改，请刷新后再保存。';
      case 'session_not_found':
        return '会话不存在，可能已被删除或归档。';
      case 'session_busy':
        return '会话正在进行中，请等待当前操作完成后再发送消息。';
      case 'run_not_found':
        return '运行记录不存在。';
      case 'approval_not_found':
        return '审批请求不存在或已过期。';
      case 'invalid_request':
        return error.message || '请求格式有误，请检查输入。';
      case 'invalid_path':
        return '文件路径不在允许的范围内。';
      case 'file_not_found':
        return '文件不存在或已被删除。';
      case 'unsupported_file_type':
        return '不支持该文件类型。';

      // World
      case 'world_not_found':
        return '世界不存在或已被删除。';
      case 'world_create_failed':
        return '创建世界失败，请检查名称是否重复。';
      case 'world_name_required':
        return '世界名称不能为空。';

      // Agent
      case 'agent_not_found':
        return '角色不存在或已被删除。';
      case 'agent_create_failed':
        return '创建角色失败，请检查必填字段。';
      case 'agent_version_conflict':
        return '角色信息已被其他操作更新，请刷新后再试。';

      // Scene
      case 'scene_not_found':
        return '场景不存在或已被删除。';
      case 'scene_create_failed':
        return '创建场景失败，请确认章节存在。';
      case 'scene_end_failed':
        return '结束场景失败，场景可能已经结束。';
      case 'scene_status_invalid':
        return '场景当前状态不支持此操作。';

      // Chapter
      case 'chapter_not_found':
        return '章节不存在或已被删除。';
      case 'chapter_update_failed':
        return '章节更新失败，请刷新后再试。';

      // Diary
      case 'information_boundary_leak':
        return '日记内容包含角色不应知晓的信息，已被信息边界过滤。';
      case 'diary_write_failed':
        return '日记写入失败，请稍后重试。';

      // Foreshadowing / Secret
      case 'foreshadow_not_found':
        return '伏笔不存在，可能已被删除。';
      case 'foreshadowing_not_found':
        return '伏笔不存在或已被删除。';
      case 'secret_not_found':
        return '秘密不存在或已被删除。';
      case 'secret_status_invalid':
        return '秘密当前状态不支持此操作。';

      // Pipeline
      case 'pipeline_not_available':
        return '创作流水线暂不可用，请确认后端已启用 worldbuilding pipeline。';
      case 'pipeline_advance_blocked':
        return '阶段推进被阻止：当前阶段条件未全部满足。请先完成当前阶段的所有要求。';
      case 'pipeline_phase_invalid':
        return '目标阶段无效，请确认阶段名称正确。';
      case 'workflow_not_found':
        return '指定的 Pipeline 工作流不存在。';

      // Image service
      case 'image_service_not_available':
        return '图片服务未启用，请确认后端 Image Service 已初始化。';
      case 'image_upload_failed':
        return '图片上传失败，请确认文件格式和大小符合要求。';
      case 'invalid_image_type':
        return '图片类型必须是头像或人设图。';
      case 'image_not_found':
        return '图片不存在或已被删除。';

      // Config
      case 'config_load_failed':
        return '配置加载失败，请检查后端服务状态。';
      case 'config_save_failed':
        return '配置保存失败，请稍后重试。';
      case 'title_generation_failed':
        return '标题生成失败，请稍后重试。';
      case 'test_failed':
        return `连接测试失败：${error.message}`;
      case 'test_unavailable':
        return '连接测试暂不可用，请检查后端配置。';

      // General
      case 'missing_param':
        return error.message || '缺少必要参数，请检查输入。';
      case 'database_error':
        return '数据库操作失败，请稍后重试。';

      default:
        return error.message || (t ? t('error.unknown') : fallback);
    }
  }
  return error instanceof Error ? error.message : t ? t('error.unknown') : fallback;
}
