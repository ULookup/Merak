# PR CI Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a GitHub Actions workflow that compiles C++ and WebUI on every PR using ubuntu-24.04 with GCC 14 / Clang 18 × Debug / Release matrix.

**Architecture:** Single workflow file `.github/workflows/ci.yml` with two jobs: `build-cpp` (4-cell compiler×build_type matrix) and `build-webui` (single Node.js build). Both must pass for CI green.

**Tech Stack:** GitHub Actions, Conan 2, CMake, GCC 14, Clang 18, Node.js 22

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `.github/workflows/ci.yml` | Create | PR build workflow |

Single file — no other changes needed. The workflow reuses caching patterns from the existing `release.yml`.

---

### Task: Create PR CI workflow

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Create the workflow file**

```yaml
name: CI

on:
  pull_request:
    paths-ignore:
      - 'docs/**'
      - 'README.md'
      - '*.md'

jobs:
  build-cpp:
    strategy:
      fail-fast: false
      matrix:
        compiler: [gcc, clang]
        build_type: [Debug, Release]
    runs-on: ubuntu-24.04
    timeout-minutes: 20
    steps:
      - uses: actions/checkout@v4

      - name: Setup GCC 14
        if: matrix.compiler == 'gcc'
        run: |
          sudo apt-get update -qq
          sudo apt-get install -y -qq g++-14
          sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100
          sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100
          echo "CC=gcc-14" >> $GITHUB_ENV
          echo "CXX=g++-14" >> $GITHUB_ENV

      - name: Setup Clang 18
        if: matrix.compiler == 'clang'
        run: |
          sudo apt-get update -qq
          sudo apt-get install -y -qq clang-18
          sudo update-alternatives --install /usr/bin/cc cc /usr/bin/clang-18 100
          sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 100
          echo "CC=clang-18" >> $GITHUB_ENV
          echo "CXX=clang++-18" >> $GITHUB_ENV

      - name: Setup Conan
        run: |
          pip install "conan>=2.0,<3.0"
          conan profile detect --name default

      - name: Configure Conan profile
        run: |
          if [ "${{ matrix.compiler }}" = "gcc" ]; then
            conan profile set settings.compiler=gcc default
            conan profile set settings.compiler.version=14 default
            conan profile set settings.compiler.libcxx=libstdc++11 default
          else
            conan profile set settings.compiler=clang default
            conan profile set settings.compiler.version=18 default
            conan profile set settings.compiler.libcxx=libstdc++ default
          fi

      - name: Cache Conan
        uses: actions/cache@v4
        with:
          path: ~/.conan2
          key: ${{ runner.os }}-conan-${{ matrix.compiler }}-${{ hashFiles('conanfile.txt') }}
          restore-keys: ${{ runner.os }}-conan-${{ matrix.compiler }}-

      - name: Conan install
        run: conan install . --build=missing -s build_type=${{ matrix.build_type }}

      - name: CMake configure
        run: cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/${{ matrix.build_type }}/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}

      - name: CMake build
        run: cmake --build build -j$(nproc)

  build-webui:
    runs-on: ubuntu-24.04
    timeout-minutes: 10
    steps:
      - uses: actions/checkout@v4

      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '22'

      - name: Cache npm
        uses: actions/cache@v4
        with:
          path: ~/.npm
          key: ${{ runner.os }}-npm-${{ hashFiles('webui/package-lock.json') }}
          restore-keys: ${{ runner.os }}-npm-

      - name: Build WebUI
        run: |
          cd webui
          npm ci
          npm run build
```

- [ ] **Step 2: Verify the file exists and is well-formed**

```bash
cat .github/workflows/ci.yml | head -5
```

Expected: shows the `name: CI` header.

- [ ] **Step 3: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml')); print('OK')"
```

Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "feat: add PR CI workflow with dual-compiler matrix build"
```

## Post-Implementation (manual)

- [ ] Push the branch, open a PR, verify CI triggers
- [ ] After CI passes at least once, configure branch protection on `main`:
  - Required checks: `build-cpp (gcc, Debug)`, `build-cpp (gcc, Release)`, `build-cpp (clang, Debug)`, `build-cpp (clang, Release)`, `build-webui`
