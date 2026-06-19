# Merak Windows Desktop Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the ten-page Merak Windows Desktop workbench from the approved screenshots, backed only by real API responses or explicitly derived data.

**Architecture:** Keep the existing React, AppState, SSE, CSS Modules, and Tauri boundaries. Add a lazy-loaded desktop shell and page modules, split API access by resource domain, and migrate existing working components into page-specific layouts without creating a parallel application.

**Tech Stack:** React 19, TypeScript 5.8, Vite 6, Vitest, Testing Library, CSS Modules, Lucide React, Tauri 2.

## Global Constraints

- Implement ten top-level pages: overview, sessions, world, characters, chapters, scenes, foreshadowing, secrets, files, settings.
- Use real HTTP API data or deterministic selectors over real responses; never ship fabricated dashboard or task data.
- Keep Tauri responsible only for local Runtime lifecycle, OS paths, and diagnostics.
- Preserve existing editor, approval, export, configuration, and desktop recovery workflows.
- Match the supplied `1672 x 941` screenshots and verify `1280 x 820` and `960 x 640` viewports.
- Keep React 19, TypeScript 5.8, Vite 6, CSS Modules, and Lucide React; add no remote-state or component-library dependency.
- Respect `prefers-reduced-motion` and keep all primary workflows keyboard reachable.
- Treat the two existing Inspector test failures as regressions to fix, not accepted baseline failures.
- Do not require `GET /v1/runs/:id/audit`; the backend does not implement it.

---

## File Structure

Create these focused modules:

- `webui/src/api/http.ts`: base URL, request helpers, API error normalization.
- `webui/src/api/runtime.ts`: runtime, session, run, approval, SSE URL operations.
- `webui/src/api/worldbuilding.ts`: world, dashboard, agent, narrative, location, faction, knowledge, graph, timeline operations.
- `webui/src/api/files.ts`: workspace file and world file-link operations.
- `webui/src/api/config.ts`: LLM and preference operations.
- `webui/src/api/index.ts`: compatibility `api` facade used during migration.
- `webui/src/hooks/useResource.ts`: cancellable page-level GET lifecycle.
- `webui/src/shell/DesktopShell.tsx`: global navigation, top bar, page outlet, responsive rails.
- `webui/src/shell/DesktopShell.module.css`: shell grid and viewport breakpoints.
- `webui/src/shell/navigation.ts`: page metadata and icon mapping.
- `webui/src/components/layout/PageState.tsx`: loading, partial error, empty, retry states.
- `webui/src/components/layout/ResourceList.tsx`: selectable keyboard list primitive.
- `webui/src/components/layout/DetailPane.tsx`: detail content and optional inspector rail.
- `webui/src/pages/*.tsx`: one composition module for each top-level page.
- `webui/src/pages/*.module.css`: page-specific layout only; shared tokens remain global.
- `webui/src/pages/selectors.ts`: deterministic overview and progress calculations.
- `webui/src/__tests__/shell.test.tsx`: shell navigation and responsive behavior.
- `webui/src/__tests__/pages.test.tsx`: page loading, empty, partial failure, and interactions.
- `webui/src/__tests__/sse-workflows.test.tsx`: ask-user and creation-request workflows.

Modify these existing modules:

- `webui/src/App.tsx`: providers, bootstrap, lazy page outlet, global overlays.
- `webui/src/AppState.tsx`: top-level page IDs, selection state, ask/creation SSE state.
- `webui/src/api/types.ts`: API resource and SSE payload types.
- `webui/src/api/client.ts`: temporary re-export during migration, then compatibility-only facade.
- `webui/src/hooks/useSSE.ts`: exported parser, reconnect timer cleanup, duplicate sequence guard.
- `webui/src/styles/global.css`: approved navy/cool-gray tokens and shared control states.
- `webui/src/i18n.tsx`: visible Chinese and English shell/page copy.
- Existing components under `webui/src/components/`: migrate into pages without duplicating working business behavior.

---

### Task 1: Normalize HTTP Errors and Split the API Foundation

**Files:**
- Create: `webui/src/api/http.ts`
- Create: `webui/src/api/runtime.ts`
- Create: `webui/src/api/index.ts`
- Modify: `webui/src/api/client.ts`
- Modify: `webui/src/__tests__/api.test.ts`

**Interfaces:**
- Produces: `request<T>(method, path, body?)`, `requestForm<T>(path, form)`, `ApiError`, `formatApiError`, `setApiBase`, `getApiBase`, `apiUrl`, and `runtimeApi`.
- Preserves: `import { api } from './api/client'` until all call sites migrate.

- [ ] **Step 1: Write failing normalization tests**

```ts
it.each([
  [{ error: { code: 'world_not_found', message: 'World not found', retryable: false } }, 'world_not_found'],
  [{ ok: false, error: { code: 'file_conflict', message: 'Conflict', retryable: true } }, 'file_conflict'],
  [{ error: 'session not found' }, undefined],
])('normalizes documented error shapes', async (payload, code) => {
  fetchMock.mockResolvedValueOnce(new Response(JSON.stringify(payload), { status: 409 }));
  await expect(request('GET', '/failure')).rejects.toMatchObject({ status: 409, code });
});
```

- [ ] **Step 2: Run the focused tests and confirm failure**

Run: `npm test -- --run src/__tests__/api.test.ts`

Expected: FAIL because `request` is not exported from `api/http.ts`.

- [ ] **Step 3: Implement the shared HTTP module**

```ts
export class ApiError extends Error {
  constructor(message: string, public status: number, public code?: string, public retryable = false) {
    super(message);
  }
}

export async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const response = await fetch(apiUrl(path), {
    method,
    headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
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
```

Move the existing base URL, form/blob helpers, and `formatApiError` unchanged in behavior. Move runtime metadata, sessions, runs, delegations, approvals, creation resolution, and SSE URL methods into `runtimeApi` in `api/runtime.ts`. Re-export a compatibility `api` object from `api/index.ts` and `api/client.ts` by spreading `runtimeApi` so existing call sites retain their method names.

- [ ] **Step 4: Run API tests and production build**

Run: `npm test -- --run src/__tests__/api.test.ts && npm run build`

Expected: API tests PASS; TypeScript and Vite build PASS.

- [ ] **Step 5: Commit the API foundation**

```powershell
git add webui/src/api/http.ts webui/src/api/runtime.ts webui/src/api/index.ts webui/src/api/client.ts webui/src/__tests__/api.test.ts
git commit -m "refactor(webui): split shared API transport"
```

### Task 2: Add Typed Worldbuilding and File Endpoints

**Files:**
- Create: `webui/src/api/worldbuilding.ts`
- Create: `webui/src/api/files.ts`
- Create: `webui/src/api/config.ts`
- Modify: `webui/src/api/types.ts`
- Modify: `webui/src/api/index.ts`
- Test: `webui/src/__tests__/api.test.ts`

**Interfaces:**
- Produces: `worldbuildingApi`, `filesApi`, `configApi`.
- Core signatures: `getDashboard(worldId)`, `listLocations(worldId)`, `listKnowledge(worldId)`, `listFactions(worldId)`, `getTimeline(worldId)`, `listGraphEntities(worldId)`, `reorderChapters(worldId, chapterIds)`, resource delete functions, `listWorldFiles(worldId)`, `linkWorldFile(worldId, link)`, `unlinkWorldFile(worldId, filePath, target)`.

- [ ] **Step 1: Add failing URL and body tests**

```ts
it('reorders chapters with the documented body', async () => {
  fetchMock.mockResolvedValueOnce(json({ ok: true }));
  await worldbuildingApi.reorderChapters('w 1', ['c1', 'c2']);
  expect(fetchMock).toHaveBeenCalledWith(
    expect.stringContaining('/api/worldbuilding/w%201/chapters/reorder'),
    expect.objectContaining({ method: 'POST', body: JSON.stringify({ order: ['c1', 'c2'] }) }),
  );
});

it('encodes a linked file path when deleting it', async () => {
  fetchMock.mockResolvedValueOnce(json({ ok: true }));
  await filesApi.unlinkWorldFile('w1', '章节/第一章.md', { entity_type: 'chapter', entity_id: 'c1' });
  expect(fetchMock.mock.calls[0][0]).toContain(encodeURIComponent('章节/第一章.md'));
  expect(fetchMock.mock.calls[0][1]).toMatchObject({
    method: 'DELETE',
    body: JSON.stringify({ target_type: 'chapter', target_id: 'c1' }),
  });
});
```

- [ ] **Step 2: Run tests and confirm missing modules**

Run: `npm test -- --run src/__tests__/api.test.ts`

Expected: FAIL on imports for `worldbuildingApi` and `filesApi`.

- [ ] **Step 3: Define exact shared resource types**

```ts
export interface ResourceListResponse<T> { ok?: boolean; items?: T[]; }
export interface LocationItem { id: string; name: string; description?: string; version?: number; }
export interface KnowledgeItem { id: string; title: string; content?: string; tags?: string[]; version?: number; }
export interface FactionItem { id: string; name: string; description?: string; version?: number; }
export interface TimelineEvent { id: string; title: string; world_time?: string; description?: string; }
export interface GraphEntity { id: string; type: string; name: string; }
export interface WorldFileLink { file_path: string; entity_type?: string; entity_id?: string; }
```

Implement every method with `encodeURIComponent` for path IDs and documented request bodies. Use response adapters that accept documented named arrays and `items` without manufacturing records.

- [ ] **Step 4: Run API tests, formatter, and build**

Run: `npm test -- --run src/__tests__/api.test.ts && npm run format:check && npm run build`

Expected: all commands PASS.

- [ ] **Step 5: Commit typed API domains**

```powershell
git add webui/src/api webui/src/__tests__/api.test.ts
git commit -m "feat(webui): add typed worldbuilding APIs"
```

### Task 3: Complete Ask-User and Creation SSE Workflows

**Files:**
- Modify: `webui/src/api/types.ts`
- Modify: `webui/src/api/runtime.ts`
- Modify: `webui/src/AppState.tsx`
- Modify: `webui/src/hooks/useSSE.ts`
- Create: `webui/src/components/AskUserPrompt.tsx`
- Create: `webui/src/components/AskUserPrompt.module.css`
- Create: `webui/src/components/CreationRequestDialog.tsx`
- Test: `webui/src/__tests__/AppState.test.ts`
- Test: `webui/src/__tests__/sse-workflows.test.tsx`

**Interfaces:**
- Produces: `runtimeApi.respondToAsk(runId: string, response: string)`, `pendingAsk`, `pendingCreation`, `RESOLVE_ASK`, `RESOLVE_CREATION` actions.
- Consumes: existing `resolveCreation(id, decision, modifications?)` behavior.

- [ ] **Step 1: Add reducer tests for SSE lifecycle and deduplication**

```ts
const askFrame = { seq: 10, type: 'ask_user_requested', payload: { run_id: 'r1', question: 'Choose POV' } };
expect(reducer(initialState, { type: 'APPLY_SSE', frame: askFrame }).pendingAsk)
  .toEqual({ runId: 'r1', question: 'Choose POV' });
expect(reducer({ ...initialState, lastSeq: 10 }, { type: 'APPLY_SSE', frame: askFrame }))
  .toEqual({ ...initialState, lastSeq: 10 });
```

- [ ] **Step 2: Run focused tests and confirm failure**

Run: `npm test -- --run src/__tests__/AppState.test.ts src/__tests__/sse-workflows.test.tsx`

Expected: FAIL because pending workflow state and dialogs do not exist.

- [ ] **Step 3: Implement typed SSE state and dialogs**

```ts
export interface PendingAsk { runId: string; question: string; choices?: string[]; }
export interface PendingCreation { id: string; toolName: string; preview?: Record<string, unknown>; }
```

Reject frames whose `seq` is nonzero and `seq <= state.lastSeq`. On submit, disable the dialog, call the real endpoint, clear state only on success, and show `formatApiError` on failure. Clear creation state on both `creation_resolved` and successful local resolution.

- [ ] **Step 4: Verify workflow tests and all reducer tests**

Run: `npm test -- --run src/__tests__/AppState.test.ts src/__tests__/sse-workflows.test.tsx`

Expected: PASS with one network call per submission and no duplicate frame effects.

- [ ] **Step 5: Commit SSE workflow completion**

```powershell
git add webui/src/api webui/src/AppState.tsx webui/src/hooks/useSSE.ts webui/src/components/AskUserPrompt* webui/src/components/CreationRequestDialog.tsx webui/src/__tests__
git commit -m "feat(webui): complete interactive SSE requests"
```

### Task 4: Build Shared Page State and Resource Layout Primitives

**Files:**
- Create: `webui/src/hooks/useResource.ts`
- Create: `webui/src/components/layout/PageState.tsx`
- Create: `webui/src/components/layout/PageState.module.css`
- Create: `webui/src/components/layout/ResourceList.tsx`
- Create: `webui/src/components/layout/ResourceList.module.css`
- Create: `webui/src/components/layout/DetailPane.tsx`
- Create: `webui/src/components/layout/DetailPane.module.css`
- Test: `webui/src/__tests__/pages.test.tsx`

**Interfaces:**
- Produces: `useResource<T>(key, loader)`, `PageState`, `ResourceList<T>`, `DetailPane`.

- [ ] **Step 1: Write failing partial-error and keyboard selection tests**

```tsx
render(<ResourceList items={[{ id: 'a', name: 'A' }, { id: 'b', name: 'B' }]} selectedId="a" getId={x => x.id} renderItem={x => x.name} onSelect={onSelect} />);
fireEvent.keyDown(screen.getByRole('listbox'), { key: 'ArrowDown' });
expect(onSelect).toHaveBeenCalledWith('b');
```

- [ ] **Step 2: Run the focused page tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx`

Expected: FAIL because layout primitives are missing.

- [ ] **Step 3: Implement cancellable resource lifecycle**

```ts
export type ResourceState<T> = { status: 'loading' | 'ready' | 'error'; data: T | null; error: Error | null; retry(): void };
export function useResource<T>(key: string, loader: (signal: AbortSignal) => Promise<T>): ResourceState<T>;
```

The hook must abort the previous request on key change, retain successful data during retry, and ignore aborted rejections. `PageState` renders skeleton, retained-data warning, full error, or empty state based on explicit props.

- [ ] **Step 4: Run tests and accessibility assertions**

Run: `npm test -- --run src/__tests__/pages.test.tsx`

Expected: PASS; listbox exposes selected option and keyboard navigation.

- [ ] **Step 5: Commit layout primitives**

```powershell
git add webui/src/hooks/useResource.ts webui/src/components/layout webui/src/__tests__/pages.test.tsx
git commit -m "feat(webui): add resource page primitives"
```

### Task 5: Add the Ten-Page Desktop Shell

**Files:**
- Create: `webui/src/shell/navigation.ts`
- Create: `webui/src/shell/DesktopShell.tsx`
- Create: `webui/src/shell/DesktopShell.module.css`
- Modify: `webui/src/AppState.tsx`
- Modify: `webui/src/App.tsx`
- Modify: `webui/src/i18n.tsx`
- Test: `webui/src/__tests__/shell.test.tsx`

**Interfaces:**
- Produces: `AppPage = 'overview' | 'sessions' | 'world' | 'characters' | 'chapters' | 'scenes' | 'foreshadowing' | 'secrets' | 'files' | 'settings' | 'editor'`.
- Produces: `DesktopShell({ page, onNavigate, children })`.

- [ ] **Step 1: Add failing navigation tests**

```tsx
const navigation = screen.getByRole('navigation', { name: '主导航' });
expect(within(navigation).getAllByRole('button')).toHaveLength(10);
fireEvent.click(within(navigation).getByRole('button', { name: /角色/ }));
expect(dispatch).toHaveBeenCalledWith({ type: 'SET_PAGE', page: 'characters' });
```

- [ ] **Step 2: Run shell tests and confirm failure**

Run: `npm test -- --run src/__tests__/shell.test.tsx`

Expected: FAIL because the desktop shell is missing.

- [ ] **Step 3: Implement navigation metadata and lazy outlet**

```ts
export const desktopPages = [
  ['overview', '概览', LayoutDashboard], ['sessions', 'Sessions 会话', MessagesSquare],
  ['world', 'World 世界设定', Globe2], ['characters', 'Characters 角色', Users],
  ['chapters', 'Chapters 章节', BookOpen], ['scenes', 'Scenes 场景', PanelsTopLeft],
  ['foreshadowing', 'Foreshadowing 伏笔线索', Sparkles], ['secrets', 'Secrets 秘密', KeyRound],
  ['files', 'Files 资料库', Files], ['settings', 'Settings 设置', Settings],
] as const;
```

Use `<button>` navigation with `aria-current="page"` rather than fake URLs. Persist the selected page in `localStorage` only after validating it against `desktopPages`. Lazy-load every page component in `App.tsx`.

- [ ] **Step 4: Run shell, reducer, and build checks**

Run: `npm test -- --run src/__tests__/shell.test.tsx src/__tests__/AppState.test.ts && npm run build`

Expected: PASS; build output contains separate page chunks.

- [ ] **Step 5: Commit the desktop shell**

```powershell
git add webui/src/shell webui/src/App.tsx webui/src/AppState.tsx webui/src/i18n.tsx webui/src/__tests__
git commit -m "feat(webui): add ten-page desktop shell"
```

### Task 6: Implement Overview Page with Deterministic Selectors

**Files:**
- Create: `webui/src/pages/selectors.ts`
- Create: `webui/src/pages/OverviewPage.tsx`
- Create: `webui/src/pages/OverviewPage.module.css`
- Modify: `webui/src/App.tsx`
- Test: `webui/src/__tests__/pages.test.tsx`

**Interfaces:**
- Produces: `selectWorldMetrics({ agents, chapters, scenes, files, overview, dashboard })`.

- [ ] **Step 1: Write failing selector tests with real response-shaped fixtures**

```ts
expect(selectWorldMetrics({ agents: [{ id: 'a' }], chapters: [{ id: 'c', status: 'completed' }], scenes: [], files: [], overview: null, dashboard: null }))
  .toMatchObject({ characterCount: 1, chapterCount: 1, completedChapterCount: 1 });
```

- [ ] **Step 2: Run selector and page tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx`

Expected: FAIL because the selector and Overview page are absent.

- [ ] **Step 3: Implement real-data sections**

Render metric strip, recent sessions, current progress, reminders derived from incomplete resources, and quick links. Omit recent activity and task widgets when no endpoint proves their content. Every metric label includes a tooltip or adjacent copy describing its source.

- [ ] **Step 4: Verify empty, partial, and populated states**

Run: `npm test -- --run src/__tests__/pages.test.tsx -t "Overview"`

Expected: PASS for dashboard success, dashboard failure with derived counts, and empty world.

- [ ] **Step 5: Commit Overview page**

```powershell
git add webui/src/pages/OverviewPage* webui/src/pages/selectors.ts webui/src/App.tsx webui/src/__tests__/pages.test.tsx
git commit -m "feat(webui): add real-data world overview"
```

### Task 7: Migrate the Sessions Workbench

**Files:**
- Create: `webui/src/pages/SessionsPage.tsx`
- Create: `webui/src/pages/SessionsPage.module.css`
- Modify: `webui/src/components/MainPanel.tsx`
- Modify: `webui/src/components/InspectorPanel.tsx`
- Modify: `webui/src/App.tsx`
- Test: `webui/src/__tests__/UserFlow.test.tsx`
- Test: `webui/src/__tests__/components.test.tsx`

**Interfaces:**
- Consumes: existing `SessionList`, `ChatTimeline`, `Composer`, `InspectorPanel`, SSE state.
- Produces: sessions-page three-column layout matching screenshot 9.

- [ ] **Step 1: Update failing workflow expectations to the approved page structure**

Assert session history, conversation title, Story/Files/Agents/Run tabs, composer, and current execution state are visible in one page.

- [ ] **Step 2: Run session tests and preserve the two known Inspector failures**

Run: `npm test -- --run src/__tests__/UserFlow.test.tsx src/__tests__/components.test.tsx`

Expected: new Sessions assertions FAIL; existing Inspector text assertions still identify the baseline mismatch.

- [ ] **Step 3: Compose the Sessions page and repair Inspector copy contracts**

Move layout ownership out of `MainPanel`; keep message, send, cancel, approval, file, and inspector behaviors unchanged. Update stale tests to assert accessible Chinese labels actually rendered by the approved UI, including `世界时间` and `Create` rather than removed English sub-tabs.

- [ ] **Step 4: Run session and component suites**

Run: `npm test -- --run src/__tests__/UserFlow.test.tsx src/__tests__/components.test.tsx`

Expected: all tests PASS, including the two baseline failures.

- [ ] **Step 5: Commit Sessions migration**

```powershell
git add webui/src/pages/SessionsPage* webui/src/components/MainPanel.tsx webui/src/components/InspectorPanel.tsx webui/src/App.tsx webui/src/__tests__
git commit -m "feat(webui): migrate sessions into desktop workspace"
```

### Task 8: Implement World and Characters Pages

**Files:**
- Create: `webui/src/pages/WorldPage.tsx`
- Create: `webui/src/pages/WorldPage.module.css`
- Create: `webui/src/pages/CharactersPage.tsx`
- Create: `webui/src/pages/CharactersPage.module.css`
- Modify: existing components under `webui/src/components/Inspector/Agent*`
- Test: `webui/src/__tests__/pages.test.tsx`

**Interfaces:**
- World consumes: world detail, locations, factions, knowledge, timeline, graph APIs.
- Characters consumes: agents, detail, images, diaries, relations, memory summaries, voice APIs.

- [ ] **Step 1: Add failing resource selection and partial-failure tests**

Verify the World page still displays world detail when graph loading fails. Verify selecting a character loads detail once and keeps the selection after list refresh.

- [ ] **Step 2: Run World and Characters tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx -t "World|Characters"`

Expected: FAIL because page modules are absent.

- [ ] **Step 3: Implement both pages with existing editors**

World uses independent `useResource` calls per section and renders only endpoint-backed sections. Characters uses `ResourceList` + `AgentCardView`/`AgentCardEdit` + an inspector rail for chapters, relations, and memories. Create, update, image upload, and delete refresh only affected resources.

- [ ] **Step 4: Verify API calls, empty states, and mutation refresh**

Run: `npm test -- --run src/__tests__/pages.test.tsx -t "World|Characters"`

Expected: PASS; failed secondary requests show inline retry and retain primary data.

- [ ] **Step 5: Commit World and Characters pages**

```powershell
git add webui/src/pages/WorldPage* webui/src/pages/CharactersPage* webui/src/components/Inspector/Agent* webui/src/__tests__/pages.test.tsx
git commit -m "feat(webui): add world and character workspaces"
```

### Task 9: Implement Chapters and Scenes Pages

**Files:**
- Create: `webui/src/pages/ChaptersPage.tsx`
- Create: `webui/src/pages/ChaptersPage.module.css`
- Create: `webui/src/pages/ScenesPage.tsx`
- Create: `webui/src/pages/ScenesPage.module.css`
- Modify: `webui/src/components/ChapterEditor.tsx`
- Modify: `webui/src/components/Inspector/EndSceneModal.tsx`
- Test: `webui/src/__tests__/pages.test.tsx`
- Test: `webui/src/__tests__/UserFlow.test.tsx`

**Interfaces:**
- Chapters produces reorder body `{ order: string[] }` and editor navigation.
- Scenes produces selected scene detail and real `endScene` result rendering.

- [ ] **Step 1: Add failing chapter reorder and scene end tests**

Assert drag-independent keyboard reorder sends exact IDs. Assert ending a scene does not mark it completed until the endpoint resolves and renders returned extraction counts.

- [ ] **Step 2: Run narrative page tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx src/__tests__/UserFlow.test.tsx -t "Chapter|Scene"`

Expected: FAIL because pages are missing.

- [ ] **Step 3: Implement chapter grid/table and scene detail composition**

Use button-based Move Previous/Next controls in addition to pointer reordering. Reuse ChapterEditor and EndSceneModal. Show consistency or extraction panels only when the scene response supplies them; otherwise omit the panel.

- [ ] **Step 4: Verify tests and editor save regression suite**

Run: `npm test -- --run src/__tests__/pages.test.tsx src/__tests__/UserFlow.test.tsx`

Expected: PASS; editor dirty/saving/ conflict behavior remains covered.

- [ ] **Step 5: Commit narrative planning pages**

```powershell
git add webui/src/pages/ChaptersPage* webui/src/pages/ScenesPage* webui/src/components/ChapterEditor.tsx webui/src/components/Inspector/EndSceneModal.tsx webui/src/__tests__
git commit -m "feat(webui): add chapter and scene workspaces"
```

### Task 10: Implement Foreshadowing and Secrets Pages

**Files:**
- Create: `webui/src/pages/ForeshadowingPage.tsx`
- Create: `webui/src/pages/ForeshadowingPage.module.css`
- Create: `webui/src/pages/SecretsPage.tsx`
- Create: `webui/src/pages/SecretsPage.module.css`
- Modify: `webui/src/components/Inspector/CreateForeshadowingModal.tsx`
- Modify: `webui/src/components/Inspector/CreateSecretModal.tsx`
- Test: `webui/src/__tests__/pages.test.tsx`

**Interfaces:**
- Consumes real foreshadowing, secret, chapter, scene, and agent data.
- Produces derived status helpers that return `null` when required fields are absent.

- [ ] **Step 1: Add failing privacy and derived-status tests**

Verify secret truth is absent from the DOM until Reveal is pressed. Verify overdue status appears only when both planned and current chapter positions are known.

- [ ] **Step 2: Run focused tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx -t "Foreshadowing|Secrets"`

Expected: FAIL because pages are missing.

- [ ] **Step 3: Implement list-detail-inspector layouts**

Use existing create modals. Add update/delete actions with confirmation. Build relation chips from real agent and narrative IDs; omit missing relations rather than inventing labels. Keep secret content in component state only after explicit reveal.

- [ ] **Step 4: Verify privacy, filtering, and mutation tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx -t "Foreshadowing|Secrets"`

Expected: PASS.

- [ ] **Step 5: Commit thread and secret workspaces**

```powershell
git add webui/src/pages/ForeshadowingPage* webui/src/pages/SecretsPage* webui/src/components/Inspector/CreateForeshadowingModal.tsx webui/src/components/Inspector/CreateSecretModal.tsx webui/src/__tests__/pages.test.tsx
git commit -m "feat(webui): add foreshadowing and secret workspaces"
```

### Task 11: Implement Files and Settings Pages

**Files:**
- Create: `webui/src/pages/FilesPage.tsx`
- Create: `webui/src/pages/FilesPage.module.css`
- Create: `webui/src/pages/SettingsPage.tsx`
- Create: `webui/src/pages/SettingsPage.module.css`
- Modify: `webui/src/components/SettingsPage.tsx`
- Modify: `webui/src/components/Inspector/FilesInspector.tsx`
- Test: `webui/src/__tests__/pages.test.tsx`
- Test: `webui/src/__tests__/SettingsPanel.test.tsx`

**Interfaces:**
- Files consumes workspace file, content, save, open, and world-file-link APIs.
- Settings consumes config, preferences, Runtime status, restart, logs, open/export diagnostics.

- [ ] **Step 1: Add failing file conflict and settings capability tests**

Verify a `409 file_conflict` preserves edited content and offers Reload/Copy. Verify settings fields without a write endpoint are not rendered as enabled controls.

- [ ] **Step 2: Run focused tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx src/__tests__/SettingsPanel.test.tsx -t "Files|Settings|desktop"`

Expected: FAIL on new page contracts.

- [ ] **Step 3: Compose full-page file library and consolidated settings**

Reuse file editor and desktop diagnostic behavior. Fetch content only after selection. Keep API keys masked. Display PostgreSQL/Neo4j status only if Runtime/config responses provide it; otherwise show the existing Runtime health status without invented database versions.

- [ ] **Step 4: Verify file, config, and desktop tests**

Run: `npm test -- --run src/__tests__/pages.test.tsx src/__tests__/SettingsPanel.test.tsx src/__tests__/DesktopBoot.test.tsx`

Expected: PASS.

- [ ] **Step 5: Commit Files and Settings pages**

```powershell
git add webui/src/pages/FilesPage* webui/src/pages/SettingsPage* webui/src/components/SettingsPage.tsx webui/src/components/Inspector/FilesInspector.tsx webui/src/__tests__
git commit -m "feat(webui): add file library and desktop settings"
```

### Task 12: Apply the Approved Visual System and Responsive Layout

**Files:**
- Modify: `webui/src/styles/global.css`
- Modify: `webui/src/shell/DesktopShell.module.css`
- Modify: all `webui/src/pages/*.module.css`
- Modify: affected existing component CSS Modules.
- Test: `webui/src/__tests__/shell.test.tsx`

**Interfaces:**
- Produces shared tokens: navy brand, cool gray surfaces, borders, typography, focus ring, semantic colors, motion durations.

- [ ] **Step 1: Add source-level token and responsive contract tests**

```ts
expect(globalCss).toContain('--brand: #06266f');
expect(shellCss).toMatch(/@media \(max-width: 1180px\)/);
expect(shellCss).toMatch(/@media \(max-width: 980px\)/);
```

- [ ] **Step 2: Run shell visual-contract tests**

Run: `npm test -- --run src/__tests__/shell.test.tsx`

Expected: FAIL until approved tokens and breakpoints are present.

- [ ] **Step 3: Implement exact shared tokens and layout behavior**

Use `#06266f` as the deep navy starting token, then sample screenshots during browser comparison and adjust tokens once globally. At `<=1180px`, collapse inspector rails behind explicit buttons. At `<=980px`, reduce sidebar width and collapse secondary resource lists into drawers; never hide the central editor or primary action. Preserve visible focus and reduced motion.

- [ ] **Step 4: Run formatting, lint, tests, and build**

Run: `npm run format:check && npm run lint && npm test -- --run && npm run build`

Expected: all commands PASS; Vite emits page chunks and no TypeScript errors.

- [ ] **Step 5: Commit visual system**

```powershell
git add webui/src/styles webui/src/shell webui/src/pages webui/src/components webui/src/__tests__/shell.test.tsx
git commit -m "style(webui): match approved desktop design system"
```

### Task 13: Browser Fidelity and Core Workflow Verification

**Files:**
- Create: `docs/qa/2026-06-19-windows-desktop-fidelity.md`
- Create: `webui/src/__tests__/DesktopNavigation.test.tsx`
- Modify: files identified by visual comparison.

**Interfaces:**
- Produces a ten-page fidelity ledger and reproducible screenshots.

- [ ] **Step 1: Start the WebUI with a controlled real or fixture API**

Run: `npm run dev -- --host 127.0.0.1`

Expected: Vite reports `http://127.0.0.1:5173`; if the local Runtime is unavailable, use test interception that returns response-shaped fixtures already used by automated tests, clearly labeling screenshots as fixture-backed rather than runtime-backed.

- [ ] **Step 2: Capture and compare every page at native design size**

Use Browser/IAB at `1672 x 941`. Capture each page, then inspect the implementation screenshot and its corresponding supplied design image with `view_image` in the same QA pass. Record copy, layout, typography, palette, icon, spacing, and interaction differences in the fidelity ledger.

- [ ] **Step 3: Fix every material mismatch and repeat comparison**

For each page, repair clipped content, wrong container model, added copy, missing controls, icon mismatch, typography drift, and spacing drift. Record only API-caused intentional deviations, with the endpoint evidence explaining each deviation.

- [ ] **Step 4: Verify default and minimum desktop windows**

Check `1280 x 820` and `960 x 640`. Navigate all ten pages, switch worlds, open a list detail, send a session message, respond to an ask, resolve a creation request, edit/save a file, open settings, and invoke a non-destructive diagnostic action. Confirm no primary content is clipped and the browser console has no errors.

- [ ] **Step 5: Run the complete verification suite**

Run: `npm run format:check && npm run lint && npm test -- --run && npm run build && npm --prefix ../apps/desktop run check`

Expected: WebUI formatting, lint, 106+ tests, and build PASS. Desktop preflight may report only the already documented missing Rust/Cargo and `merak.exe`; any source/config error fails this task.

- [ ] **Step 6: Commit final QA fixes and ledger**

```powershell
git add webui docs/qa/2026-06-19-windows-desktop-fidelity.md
git commit -m "test(webui): verify Windows desktop redesign"
```

---

## Completion Audit

Before claiming the goal complete, verify each requirement against authoritative evidence:

1. `desktopPages` contains exactly ten top-level workspaces and every page is reachable through keyboard navigation.
2. Search page code for hard-coded resource arrays; only navigation metadata and empty UI labels are allowed.
3. Map every visible statistic and relationship in the fidelity ledger to an API field or named selector.
4. Exercise `ask_user_requested`, ask response, `creation_requested`, and creation resolution end to end.
5. Exercise editor save/conflict, approval, export, config save/test, Runtime restart, and diagnostics.
6. Confirm the full Vitest suite, lint, formatting, and Vite build pass from fresh commands.
7. Compare all ten page screenshots at `1672 x 941`, then verify `1280 x 820` and `960 x 640`.
8. Confirm remaining deviations are listed with API or environment evidence.
9. Confirm the only native build blocker is the externally missing Rust/Cargo and `merak.exe`; do not mark the goal complete while any source-level desktop failure remains.
