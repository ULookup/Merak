# WebUI Client Fixes & Page Architecture

**Date:** 2026-06-17
**Issues:** #132, #133, #134, #135, #136, #137, #143

## Overview

Fix 6 P0 client-side bugs and introduce page-level architecture to replace the single-view phase-based rendering.

## Problem

All functionality is crammed into `App.tsx` phase rendering (loading → no_world → no_agent → ready). Settings is buried in a sidebar panel. ChapterEditor exists but is never used. Several APIs exist but aren't wired to UI.

## Design

### Page Architecture

State-based routing (no react-router; Tauri desktop app doesn't need URL routing). Add `currentPage` to AppState:

```
App
├── Workbench    — three-column layout (Sidebar / ChatTimeline+Composer / Inspector)
├── Settings     — merged LLM config + user preferences, full page
├── WorldSetup   — existing onboarding / world dashboard flow
└── ChapterEditor (overlay) — full-screen editor, opened from chapter list in StoryInspector
```

Navigation:
- Workbench ↔ Settings: gear icon in sidebar bottom
- Workbench → ChapterEditor: click chapter in StoryInspector chapter list
- ChapterEditor → Workbench: back button / Esc key
- WorldSetup: seamless transition during bootstrap

### Issue Fixes

| Issue | Fix |
|-------|-----|
| #136 dead SettingsPanel | Delete `components/SettingsPanel.tsx` + `.module.css`. Merge preferences logic into new Settings page. |
| #135 preferences not loaded | Call `api.getPreferences()` in App bootstrap, dispatch `SET_USER_PREFERENCES`. Also load in Settings page mount. |
| #134 pipeline auto-advance no toggle | Add `SET_PIPELINE_AUTO_ADVANCE` action + reducer case. Make badge in PipelineNavigator clickable. |
| #133 time advance button disabled | Remove `disabled`. Add text input for new world time. Wire `api.advanceWorldTime()` on click. |
| #143 error messages not localized | Extend `formatApiError()` with 15+ Chinese error code mappings for worldbuilding domain. |
| #132 ChapterEditor not integrated | Add chapter list in StoryInspector. Click chapter opens full-screen ChapterEditor overlay. |
| #137 ModalBase.module.css unused | False positive. Used via CSS Modules `composes` in CreateModal.module.css and EndSceneModal.module.css. Close without code change. |

### Files

**New:**
- `src/components/SettingsPage.tsx` + `.module.css` — merged LLM config + user preferences

**Deleted:**
- `src/components/SettingsPanel.tsx` + `.module.css` — dead code

**Modified:**
- `App.tsx` — page router, preferences loading
- `AppState.tsx` — `currentPage`, `SET_PAGE`, `SET_PIPELINE_AUTO_ADVANCE`
- `StoryInspector.tsx` — chapter list, time advance wiring
- `PipelineNavigator.tsx` — auto/manual toggle
- `WorldSidebar.tsx` — gear icon navigates to Settings page
- `api/client.ts` — `formatApiError()` Chinese mappings

## Error Handling

- Preferences load failure: non-blocking, defaults preserved
- Time advance failure: toast with Chinese error message
- Chapter content fetch failure: ChapterEditor shows empty state with retry
- Settings save failure: inline error, don't clear form
