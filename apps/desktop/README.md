# Merak Desktop

Tauri desktop shell for the existing Merak WebUI.

## Development

From the repository root:

```bash
npm run desktop:dev
```

This starts the existing `webui` Vite dev server and opens it inside a Tauri desktop window.

## Windows Package

Run on Windows:

```powershell
npm install
npm --prefix webui install
npm --prefix apps/desktop install
npm run desktop:build
```

The Tauri config targets Windows installers:

- MSI
- NSIS

## Runtime Boundary

This shell packages the current WebUI. It does not yet start or supervise the Merak runtime process.
During development and packaged use, the WebUI still expects the Merak API/SSE runtime to be reachable
at the configured backend address.

The WebUI detects the Tauri environment and uses desktop-specific runtime connection copy when the
backend is unavailable. Runtime process management, tray behavior, and auto-update are intentionally
left for later desktop-focused iterations.
