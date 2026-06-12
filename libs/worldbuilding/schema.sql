-- Merak Worldbuilding — PostgreSQL Schema
-- Requires: zhparser (Chinese full-text search), pgvector (vector embeddings)

-- ─── Extensions ────────────────────────────────────────────────────

CREATE EXTENSION IF NOT EXISTS zhparser;
CREATE EXTENSION IF NOT EXISTS vector;

-- ─── Custom text search config for Chinese ─────────────────────────

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_ts_config WHERE cfgname = 'chinese') THEN
        CREATE TEXT SEARCH CONFIGURATION chinese (PARSER = zhparser);
        ALTER TEXT SEARCH CONFIGURATION chinese
            ADD MAPPING FOR n,v,a,i,e,l WITH simple;
    END IF;
END
$$;

-- ─── Worlds ────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS worlds (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    description TEXT NOT NULL,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

-- ─── World Knowledge ───────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS world_knowledge (
    id          TEXT PRIMARY KEY,
    world_id    TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    category    TEXT NOT NULL,
    content     TEXT NOT NULL,
    created_at  TEXT NOT NULL,
    tags        TEXT NOT NULL DEFAULT '[]',
    aliases     TEXT NOT NULL DEFAULT '[]',
    related_ids TEXT NOT NULL DEFAULT '[]'
);

CREATE INDEX IF NOT EXISTS idx_world_knowledge_world
    ON world_knowledge(world_id, category);

-- Chinese FTS on world_knowledge
ALTER TABLE world_knowledge ADD COLUMN IF NOT EXISTS content_tsv tsvector;
CREATE INDEX IF NOT EXISTS idx_world_knowledge_fts
    ON world_knowledge USING gin(content_tsv);

-- Trigger to keep tsvector in sync
CREATE OR REPLACE FUNCTION world_knowledge_tsv_trigger() RETURNS trigger AS $$
BEGIN
    NEW.content_tsv := to_tsvector('chinese', coalesce(NEW.content, ''));
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_world_knowledge_tsv ON world_knowledge;
CREATE TRIGGER trg_world_knowledge_tsv
    BEFORE INSERT OR UPDATE OF content ON world_knowledge
    FOR EACH ROW EXECUTE FUNCTION world_knowledge_tsv_trigger();

-- ─── Locations ─────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS locations (
    id                  TEXT PRIMARY KEY,
    world_id            TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    name                TEXT NOT NULL,
    description         TEXT NOT NULL,
    region              TEXT NOT NULL,
    parent_location_id  TEXT,
    created_at          TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_locations_world
    ON locations(world_id);

-- ─── Agents ────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS agents (
    id           TEXT PRIMARY KEY,
    world_id     TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    name         TEXT NOT NULL,
    display_name TEXT NOT NULL,
    kind         TEXT NOT NULL,
    created_at   TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_agents_world ON agents(world_id);

CREATE UNIQUE INDEX IF NOT EXISTS one_god_agent_per_world
    ON agents(world_id, kind) WHERE kind = 'god';

-- ─── Agent Metadata ────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS agent_metadata (
    agent_id           TEXT PRIMARY KEY REFERENCES agents(id) ON DELETE CASCADE,
    can_speak_directly INTEGER NOT NULL
);

-- ─── Agent Images ───────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS agent_images (
    id               TEXT PRIMARY KEY,
    agent_id         TEXT NOT NULL REFERENCES agents(id) ON DELETE CASCADE,
    image_type       TEXT NOT NULL CHECK (image_type IN ('avatar', 'design')),
    storage_key      TEXT NOT NULL,
    mime_type        TEXT NOT NULL DEFAULT 'image/png',
    original_name    TEXT,
    file_size_bytes  INTEGER NOT NULL DEFAULT 0,
    is_primary       BOOLEAN NOT NULL DEFAULT false,
    sort_order       INTEGER NOT NULL DEFAULT 0,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_agent_images_agent ON agent_images(agent_id, image_type);

-- ─── Character Cards (latest version for search) ───────────────────

CREATE TABLE IF NOT EXISTS character_cards (
    agent_id           TEXT PRIMARY KEY REFERENCES agents(id) ON DELETE CASCADE,
    name               TEXT NOT NULL,
    age                INTEGER NOT NULL DEFAULT 0,
    gender             TEXT NOT NULL DEFAULT '',
    race               TEXT NOT NULL DEFAULT '',
    identity           TEXT NOT NULL DEFAULT '',
    core_traits        TEXT[] NOT NULL DEFAULT '{}',
    emotional_tendency TEXT NOT NULL DEFAULT '',
    speaking_style     TEXT NOT NULL DEFAULT '',
    taboo_topics       TEXT[] NOT NULL DEFAULT '{}',
    core_desire        TEXT NOT NULL DEFAULT '',
    deep_fear          TEXT NOT NULL DEFAULT '',
    daily_goal         TEXT NOT NULL DEFAULT '',
    background         TEXT NOT NULL DEFAULT '',
    knowledge_scope    TEXT NOT NULL DEFAULT '',
    appearance         TEXT NOT NULL DEFAULT '',
    version            INTEGER NOT NULL DEFAULT 1,
    updated_at         TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_character_cards_traits
    ON character_cards USING gin(core_traits);

CREATE INDEX IF NOT EXISTS idx_character_cards_identity
    ON character_cards(identity);

-- ─── Agent Diaries ─────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS agent_diaries (
    id          TEXT PRIMARY KEY,
    agent_id    TEXT NOT NULL REFERENCES agents(id) ON DELETE CASCADE,
    scene_id    TEXT NOT NULL,
    world_time  TEXT NOT NULL,
    content     TEXT NOT NULL,
    created_at  TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_diaries_agent ON agent_diaries(agent_id, created_at DESC);

ALTER TABLE agent_diaries ADD COLUMN IF NOT EXISTS content_tsv tsvector;
CREATE INDEX IF NOT EXISTS idx_diaries_fts ON agent_diaries USING gin(content_tsv);

CREATE OR REPLACE FUNCTION agent_diaries_tsv_trigger() RETURNS trigger AS $$
BEGIN
    NEW.content_tsv := to_tsvector('chinese', coalesce(NEW.content, ''));
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_agent_diaries_tsv ON agent_diaries;
CREATE TRIGGER trg_agent_diaries_tsv
    BEFORE INSERT OR UPDATE OF content ON agent_diaries
    FOR EACH ROW EXECUTE FUNCTION agent_diaries_tsv_trigger();

-- ─── Memory Summaries ──────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS memory_summaries (
    id               TEXT PRIMARY KEY,
    agent_id         TEXT NOT NULL REFERENCES agents(id) ON DELETE CASCADE,
    period_start     TEXT NOT NULL,
    period_end       TEXT NOT NULL,
    summary          TEXT NOT NULL,
    source_diary_ids TEXT NOT NULL,
    created_at       TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_memory_summaries_agent
    ON memory_summaries(agent_id, created_at DESC);

-- ─── Agent Relations ───────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS agent_relations (
    agent_id      TEXT NOT NULL REFERENCES agents(id) ON DELETE CASCADE,
    target_id     TEXT NOT NULL REFERENCES agents(id) ON DELETE CASCADE,
    relation_type TEXT NOT NULL,
    description   TEXT NOT NULL,
    intimacy      INTEGER NOT NULL,
    key_events    TEXT NOT NULL,
    updated_at    TEXT NOT NULL,
    PRIMARY KEY(agent_id, target_id)
);

-- ─── Hybrid Search Function ────────────────────────────────────────

CREATE OR REPLACE FUNCTION hybrid_search_diary(
    p_agent_id  TEXT,
    p_query     TEXT,
    p_max       INTEGER DEFAULT 20
) RETURNS TABLE(
    id          TEXT,
    agent_id    TEXT,
    scene_id    TEXT,
    world_time  TEXT,
    content     TEXT,
    created_at  TEXT,
    rank        REAL
) AS $$
DECLARE
    query_tsquery tsquery;
BEGIN
    query_tsquery := plainto_tsquery('chinese', p_query);
    RETURN QUERY
    SELECT d.id, d.agent_id, d.scene_id, d.world_time, d.content, d.created_at,
           ts_rank_cd(d.content_tsv, query_tsquery)::real AS rank
    FROM agent_diaries d
    WHERE d.agent_id = p_agent_id
      AND d.content_tsv @@ query_tsquery
    ORDER BY rank DESC
    LIMIT p_max;
END;
$$ LANGUAGE plpgsql STABLE;

-- ─── Hybrid Search for World Knowledge ─────────────────────────────

CREATE OR REPLACE FUNCTION hybrid_search_knowledge(
    p_world_id  TEXT,
    p_query     TEXT,
    p_category  TEXT DEFAULT '',
    p_max       INTEGER DEFAULT 20
) RETURNS TABLE(
    id          TEXT,
    category    TEXT,
    content     TEXT,
    created_at  TEXT,
    tags        TEXT,
    aliases     TEXT,
    related_ids TEXT,
    rank        REAL
) AS $$
DECLARE
    query_tsquery tsquery;
BEGIN
    query_tsquery := plainto_tsquery('chinese', p_query);
    RETURN QUERY
    SELECT k.id, k.category, k.content, k.created_at,
           k.tags, k.aliases, k.related_ids,
           ts_rank_cd(k.content_tsv, query_tsquery)::real AS rank
    FROM world_knowledge k
    WHERE k.world_id = p_world_id
      AND k.content_tsv @@ query_tsquery
      AND (p_category = '' OR k.category = p_category)
    ORDER BY rank DESC
    LIMIT p_max;
END;
$$ LANGUAGE plpgsql STABLE;

-- ─── Backfill existing tsvector columns ────────────────────────────

UPDATE world_knowledge SET content_tsv = to_tsvector('chinese', coalesce(content, ''))
    WHERE content_tsv IS NULL;
UPDATE agent_diaries SET content_tsv = to_tsvector('chinese', coalesce(content, ''))
    WHERE content_tsv IS NULL;

-- ─── Narrative: Arcs ──────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS arcs (
    id          TEXT PRIMARY KEY,
    world_id    TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    name        TEXT,
    description TEXT,
    theme       TEXT,
    status      TEXT,
    metadata    JSONB DEFAULT '{}',
    created_at  TIMESTAMPTZ DEFAULT now(),
    updated_at  TIMESTAMPTZ DEFAULT now()
);

-- ─── Narrative: Chapters ──────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS chapters (
    id          TEXT PRIMARY KEY,
    world_id    TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    arc_id      TEXT,
    name        TEXT,
    pitch       TEXT,
    status      TEXT,
    position    INT DEFAULT 0,
    created_at  TIMESTAMPTZ DEFAULT now(),
    updated_at  TIMESTAMPTZ DEFAULT now()
);

-- ─── Narrative: Sections ──────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS sections (
    id          TEXT PRIMARY KEY,
    world_id    TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    chapter_id  TEXT NOT NULL,
    name        TEXT,
    status      TEXT,
    position    INT DEFAULT 0,
    created_at  TIMESTAMPTZ DEFAULT now(),
    updated_at  TIMESTAMPTZ DEFAULT now()
);

-- ─── Narrative: Scenes ────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS scenes (
    id               TEXT PRIMARY KEY,
    world_id         TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    chapter_id       TEXT,
    section_id       TEXT,
    name             TEXT,
    pitch            TEXT,
    status           TEXT,
    participants     TEXT DEFAULT '[]',
    pov_character_id TEXT,
    location         TEXT,
    world_time       TEXT,
    scene_time       TEXT,
    is_flashback     BOOLEAN DEFAULT false,
    scene_index      INT DEFAULT 0,
    created_at       TIMESTAMPTZ DEFAULT now(),
    updated_at       TIMESTAMPTZ DEFAULT now()
);

CREATE INDEX IF NOT EXISTS scenes_by_world_time
    ON scenes(world_id, world_time, id);

-- ─── Narrative: Timeline Events ───────────────────────────────────────

CREATE TABLE IF NOT EXISTS timeline_events (
    id          TEXT PRIMARY KEY,
    world_id    TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    scene_id    TEXT,
    event       TEXT,
    world_time  TEXT,
    created_at  TIMESTAMPTZ DEFAULT now()
);

-- ─── Foreshadowing ────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS foreshadowings (
    id              TEXT PRIMARY KEY,
    world_id        TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    hint            TEXT,
    hint_level      TEXT DEFAULT 'subtle',
    status          TEXT DEFAULT 'open',
    created_by      TEXT DEFAULT 'author',
    pay_off_scene_id TEXT,
    pay_off         TEXT,
    created_at      TIMESTAMPTZ DEFAULT now(),
    updated_at      TIMESTAMPTZ DEFAULT now()
);

CREATE INDEX IF NOT EXISTS foreshadowings_by_status
    ON foreshadowings(world_id, status, id);

-- ─── Secrets ──────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS secrets (
    id                  TEXT PRIMARY KEY,
    world_id            TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    secret_type         TEXT DEFAULT 'background',
    status              TEXT DEFAULT 'active',
    holder_ids          TEXT DEFAULT '[]',
    known_by_ids        TEXT DEFAULT '[]',
    content             TEXT,
    stakes              TEXT,
    deeper_truth        TEXT,
    exposed_in_scene_id TEXT,
    created_at          TIMESTAMPTZ DEFAULT now(),
    updated_at          TIMESTAMPTZ DEFAULT now()
);

CREATE INDEX IF NOT EXISTS secrets_by_status
    ON secrets(world_id, status, id);

CREATE INDEX IF NOT EXISTS secrets_by_holder
    ON secrets(world_id, holder_ids, id);

-- ─── Agent Prompts (system prompts written by Creative Director) ────

CREATE TABLE IF NOT EXISTS agent_prompts (
    agent_id     TEXT PRIMARY KEY REFERENCES agents(id) ON DELETE CASCADE,
    prompt       TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);
