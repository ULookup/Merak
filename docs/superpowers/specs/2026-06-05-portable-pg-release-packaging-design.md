# Design: Bundled PostgreSQL for Release Packaging

**Date:** 2026-06-05
**Status:** Design Approved

## Context

Merak uses PostgreSQL for memory and worldbuilding. The `PortablePg` class in `libs/portable_pg/` already manages the lifecycle (initdb, start, stop, port allocation), and `main.cpp` auto-launches it from a `pg/` directory next to the executable. However, the release workflow does not include PostgreSQL binaries in the package, so users must install PG separately.

## Goal

Bundle PostgreSQL 17 (full EDB distribution) into the release tarball/zip so users can run Merak without installing PostgreSQL.

## Decisions

| Decision | Choice |
|----------|--------|
| PG source | EDB official precompiled binaries |
| Version | PostgreSQL 17.4 |
| Platforms | Linux x64 + Windows x64 |
| Package scope | Full distribution (no file trimming) |

## Design

### 1. Release Package Layout

```
merak/
├── merak              # C++ binary (serve + tui)
├── start.sh           # One-click launcher
├── webui/             # React static files
├── config/            # Prompt templates
└── pg/                # PostgreSQL 17 portable
    ├── bin/           # postgres, pg_ctl, initdb, psql, ...
    ├── lib/           # Runtime libraries
    ├── share/         # Timezone/encoding data
    └── data/          # Created at runtime by initdb
```

This matches `PortablePg` expectations: `bin_dir()` returns `pg_dir_ / "bin"`, `data_dir()` returns `pg_dir_ / "data"`.

### 2. CI Release Workflow Changes

**File:** `.github/workflows/release.yml`

Add PG download steps in both `build-linux` and `build-windows` jobs, inside "Assemble green package":

**Linux:**
```yaml
- name: Download PostgreSQL 17 (Linux)
  run: |
    PG_VERSION="17.4"
    curl -fsSL "https://get.enterprisedb.com/postgresql/postgresql-${PG_VERSION}-1-linux-x64-binaries.tar.gz" \
      -o /tmp/pg.tar.gz
    tar xzf /tmp/pg.tar.gz -C /tmp
    mv /tmp/pgsql merak-pkg/pg
```

**Windows:**
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

- EDB archive extracts to `pgsql/` — renamed to `pg/`
- `PG_VERSION` defined in workflow env for centralized version management
- Download failure fails the CI job (no broken release)

### 3. Auto-Enable Memory

**File:** `cli/src/main.cpp`, line 121

When portable PG starts, also set `memory.enabled = true` so schema tables are created:

```cpp
if (portable_pg->start()) {
    cfg.memory.db_connection = portable_pg->connection_string();
    cfg.memory.enabled = true;  // ★ added
    std::cout << "Portable PostgreSQL started on port " << portable_pg->port() << "\n";
}
```

This ensures `MemoryStore::init_db()` runs (uses `CREATE TABLE IF NOT EXISTS`) and `WorldbuildingService` initializes.

### 4. Error Handling

| Scenario | Behavior |
|----------|----------|
| `pg/` directory absent (source build) | Skip portable PG, continue without it |
| PG binaries corrupted | `start()` fails, print warning, continue serve |
| Port conflict | `find_free_port()` scans 15432-16431 |
| initdb fails | Print error, release resources, continue serve |
| `data/` already exists | Skip initdb, reuse existing data (data survives restarts) |

### 5. Verification

- Local smoke test: place PG binaries in `pg/` next to binary, run `./merak serve` — should print "Portable PostgreSQL started on port ..."
- TUI: `/world list` should work (no "Worldbuilding API is not available")
- CI artifact check: `tar tzf merak-linux-x64.tar.gz | grep pg/bin/postgres`
- Persistence test: restart serve, previous world data still accessible
