# Merak Desktop

Windows desktop client for the Merak creation workbench.

## Development

From the repository root:

```bash
npm run desktop:dev
```

This starts the WebUI Vite dev server and opens it inside a Tauri desktop window. The desktop
shell also supervises the local Merak service used by the workbench.

## Windows Package

Prerequisites on Windows:

- Node.js and npm
- Rust with Cargo available on `PATH`
- Visual Studio Build Tools with the C++ desktop workload
- A built Merak runtime executable, or `MERAK_RUNTIME_EXE` pointing to an existing `merak.exe`

Run from the repository root:

```powershell
npm install
npm --prefix webui install
npm --prefix apps/desktop install
npm --prefix apps/desktop run check
npm run desktop:build
```

The Tauri config targets Windows installers:

- MSI
- NSIS

## Local Service

The desktop shell prepares a local Merak folder for settings, story data, and logs. On launch it
starts the bundled `merak.exe` when available, chooses a free local port, and points the WebUI at that
local service.

If startup fails, the app shows a plain-language setup or recovery screen. Users can reopen Merak,
open the report folder, or export a report file from the desktop UI.

## Runtime Executable

The desktop installer must include the Merak CLI runtime as `merak.exe`. During packaging, the
preflight script looks for it in:

- `apps/desktop/resources/merak.exe`
- `build/cli/Release/merak.exe`
- `build/cli/Debug/merak.exe`
- `build/cli/merak.exe`
- the path provided by `MERAK_RUNTIME_EXE`

If your local build writes `merak.exe` somewhere else, set `MERAK_RUNTIME_EXE` before running
`npm run desktop:build`.

## Release Notes

The app is configured to build Windows MSI and NSIS installers. Before Tauri packaging starts,
`apps/desktop/scripts/check-prereqs.mjs` reports missing local prerequisites, then
`apps/desktop/scripts/prepare-runtime.mjs` copies the runtime executable into
`apps/desktop/resources/merak.exe`. It checks common CMake output folders and the optional
`MERAK_RUNTIME_EXE` environment variable.
