# Portable PostgreSQL Release Packaging — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bundle PostgreSQL 17 into the Merak release package so users can run without installing PG.

**Architecture:** The `PortablePg` class and `main.cpp` already handle lifecycle — two gaps remain: (1) the release CI doesn't include PG binaries, (2) `memory.enabled` stays false when portable PG auto-starts, so schema tables are never created. Two files changed, no new files.

**Tech Stack:** Bash (CI steps), C++23 (main.cpp), EDB PostgreSQL 17 binaries.

---

### Task 1: Auto-enable memory when portable PG starts

**Files:**
- Modify: `cli/src/main.cpp:121`

- [ ] **Step 1: Add `cfg.memory.enabled = true;`**

In `cli/src/main.cpp`, after line 121 (`cfg.memory.db_connection = portable_pg->connection_string();`), add the enable line:

```cpp
if (portable_pg->start()) {
    cfg.memory.db_connection = portable_pg->connection_string();
    cfg.memory.enabled = true;
    std::cout << "Portable PostgreSQL started on port " << portable_pg->port() << "\n";
}
```

- [ ] **Step 2: Build and verify compilation**

```bash
cd /home/icepop/Merak/build && cmake --build . -j
```

Expected: Compiles without errors.

- [ ] **Step 3: Commit**

```bash
cd /home/icepop/Merak
git add cli/src/main.cpp
git commit -m "feat: auto-enable memory when portable PG starts
When portable PostgreSQL is bundled and auto-started, memory.enabled
was staying false from the template config, preventing schema init."
```

---

### Task 2: Bundle PG binaries in release CI

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Add PG download step in build-linux job**

In `.github/workflows/release.yml`, inside the `build-linux` job, after the existing "Assemble green package" step's `cp` commands and before the "Package" step, insert:

```yaml
      - name: Download PostgreSQL 17 (Linux)
        run: |
          PG_VERSION="17.4"
          curl -fsSL "https://get.enterprisedb.com/postgresql/postgresql-${PG_VERSION}-1-linux-x64-binaries.tar.gz" \
            -o /tmp/pg.tar.gz
          tar xzf /tmp/pg.tar.gz -C /tmp
          mv /tmp/pgsql merak-pkg/pg
```

The surrounding context should look like:

```yaml
      - name: Assemble green package
        run: |
          mkdir -p merak-pkg/webui merak-pkg/config
          cp build/cli/merak merak-pkg/
          cp -r webui/dist/* merak-pkg/webui/
          cp config/prompts merak-pkg/config/ -r 2>/dev/null || true
          cp scripts/start.sh merak-pkg/ 2>/dev/null || true

      - name: Download PostgreSQL 17 (Linux)
        run: |
          PG_VERSION="17.4"
          curl -fsSL "https://get.enterprisedb.com/postgresql/postgresql-${PG_VERSION}-1-linux-x64-binaries.tar.gz" \
            -o /tmp/pg.tar.gz
          tar xzf /tmp/pg.tar.gz -C /tmp
          mv /tmp/pgsql merak-pkg/pg

      - name: Package
        run: tar czf merak-linux-x64.tar.gz -C merak-pkg .
```

- [ ] **Step 2: Add PG download step in build-windows job**

In the same file, inside the `build-windows` job, after the existing "Assemble green package" step and before the "Package" step, insert:

```yaml
      - name: Download PostgreSQL 17 (Windows)
        shell: bash
        run: |
          PG_VERSION="17.4"
          curl -fsSL "https://get.enterprisedb.com/postgresql/postgresql-${PG_VERSION}-1-windows-x64-binaries.zip" \
            -o /tmp/pg.zip
          unzip -q /tmp/pg.zip -d /tmp
          mv /tmp/pgsql merak-pkg/pg
```

The surrounding context should look like:

```yaml
      - name: Assemble green package
        shell: bash
        run: |
          mkdir -p merak-pkg/webui merak-pkg/config
          cp build/cli/Release/merak.exe merak-pkg/ || cp build/cli/merak.exe merak-pkg/
          cp build/cli/Release/merak-launcher.exe merak-pkg/ 2>/dev/null || true
          cp -r webui/dist/* merak-pkg/webui/
          cp config/prompts merak-pkg/config/ -r 2>/dev/null || true

      - name: Download PostgreSQL 17 (Windows)
        shell: bash
        run: |
          PG_VERSION="17.4"
          curl -fsSL "https://get.enterprisedb.com/postgresql/postgresql-${PG_VERSION}-1-windows-x64-binaries.zip" \
            -o /tmp/pg.zip
          unzip -q /tmp/pg.zip -d /tmp
          mv /tmp/pgsql merak-pkg/pg

      - name: Package
        shell: bash
        run: |
          7z a merak-windows-x64.zip ./merak-pkg/*
```

- [ ] **Step 3: Commit**

```bash
cd /home/icepop/Merak
git add .github/workflows/release.yml
git commit -m "feat: bundle PostgreSQL 17 in release packages

Add EDB PostgreSQL 17 download steps in build-linux and build-windows
jobs so release artifacts include a portable pg/ directory."
```

---

### Post-Implementation Verification

These steps require a working build — run them after both tasks are committed:

- [ ] **Smoke test: build from source behaves correctly (no pg/ dir)**

```bash
cd /home/icepop/Merak/build
cmake --build . -j
./cli/merak serve --port 13999 &
sleep 2
# Should start without "Portable PostgreSQL" message (no pg/ dir)
curl -s http://127.0.0.1:13999/v1/runtime | head -c 200
kill %1 2>/dev/null
```

Expected: Server starts. No "Portable PostgreSQL" in output (since no `pg/` directory next to the build binary).

- [ ] **Smoke test: simulate release layout**

```bash
cd /home/icepop/Merak/build
mkdir -p /tmp/merak-test/pg/bin
# Copy system PG binaries to simulate bundled PG
cp /usr/lib/postgresql/*/bin/postgres /tmp/merak-test/pg/bin/ 2>/dev/null || \
  cp $(which postgres) /tmp/merak-test/pg/bin/ 2>/dev/null || \
  echo "SKIP: no system PG to test with"
cp /usr/lib/postgresql/*/bin/pg_ctl /tmp/merak-test/pg/bin/ 2>/dev/null || true
cp /usr/lib/postgresql/*/bin/initdb /tmp/merak-test/pg/bin/ 2>/dev/null || true
cp /usr/lib/postgresql/*/bin/psql /tmp/merak-test/pg/bin/ 2>/dev/null || true
cp -r /usr/lib/postgresql/*/lib /tmp/merak-test/pg/ 2>/dev/null || true
cp -r /usr/share/postgresql /tmp/merak-test/pg/share 2>/dev/null || true
cp ./cli/merak /tmp/merak-test/
# Start and check for "Portable PostgreSQL started"
/tmp/merak-test/merak serve --port 13998 &
sleep 3
kill %1 2>/dev/null
rm -rf /tmp/merak-test
```

