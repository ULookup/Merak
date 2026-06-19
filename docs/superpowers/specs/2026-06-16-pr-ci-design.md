# PR CI Pipeline Design

## Summary

A GitHub Actions CI workflow that compiles the full project on every pull request against a Linux environment (ubuntu-24.04). C++ (GCC 14 / Clang 18, Debug / Release) and WebUI must both build successfully before merging is allowed.

## Trigger

- `pull_request` — all PRs, any base branch
- `paths-ignore`: `docs/**`, `README.md`, `*.md` — skip pure-documentation changes

## Environment

- `ubuntu-24.04` — provides GCC 14 and Clang 18 natively, full C++23 support

## Jobs

### build-cpp (matrix × 4)

C++ compilation across compiler and build-type dimensions:

| axis | values |
|------|--------|
| compiler | `gcc` (GCC 14), `clang` (Clang 18) |
| build_type | `Debug`, `Release` |

Steps per matrix cell:

1. **Checkout** — `actions/checkout@v4`
2. **Setup compiler** — install and set CC/CXX per compiler selection
3. **Setup Conan** — pip install conan>=2.0,<3.0, profile detect
4. **Restore Conan cache** — key includes compiler dimension
5. **Conan install** — `conan install . --build=missing -s build_type=$BUILD_TYPE`
6. **CMake configure** — toolchain from `build/$BUILD_TYPE/generators/conan_toolchain.cmake`
7. **CMake build** — `cmake --build build -j$(nproc)`

`fail-fast: false` so all matrix cells complete independently.

### build-webui (× 1)

Single job for the Node.js frontend:

1. **Checkout**
2. **Setup Node 22**
3. **Restore npm cache**
4. **npm ci && npm run build**

## Caching

- Conan: `key: ${{ runner.os }}-conan-${{ matrix.compiler }}-${{ hashFiles('conanfile.txt') }}`
- npm: `key: ${{ runner.os }}-npm-${{ hashFiles('webui/package-lock.json') }}`

Conan cache key includes compiler to avoid GCC/Clang binary incompatibilities.

## Branch Protection (manual)

After the workflow exists and at least one PR build has completed, configure branch protection on `main`:

- Require the following status checks to pass before merging:
  - `build-cpp (gcc, Debug)`
  - `build-cpp (gcc, Release)`
  - `build-cpp (clang, Debug)`
  - `build-cpp (clang, Release)`
  - `build-webui`

## Out of Scope

- C++ tests (GTest) — not included in this PR check; may be added in a future iteration
- Windows / macOS builds — already covered by release workflow for tagged builds
- Sanitizer builds (ASan, UBSan) — incremental addition possible under a separate matrix axis
