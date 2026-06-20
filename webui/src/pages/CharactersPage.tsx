import { useState } from 'react';
import { Plus, RefreshCw, Users } from 'lucide-react';
import { api } from '../api/client';
import type {
  DiaryEntry,
  MemorySummary,
  RelationEntry,
  VoiceFingerprint,
  WorldAgent,
} from '../api/types';
import AgentAvatar from '../components/AgentAvatar';
import AgentCardView from '../components/Inspector/AgentCardView';
import CreateAgentModal from '../components/Inspector/CreateAgentModal';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import styles from './CharactersPage.module.css';

interface CharactersPageProps {
  worldId: string;
}

export default function CharactersPage({ worldId }: CharactersPageProps) {
  const agents = useResource(`characters:${worldId}`, () => api.listAgents(worldId));
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [showCreate, setShowCreate] = useState(false);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const items = agents.data?.agents ?? [];
  const selected = selectedId && items.some((item) => item.id === selectedId) ? selectedId : null;

  async function deleteSelected() {
    if (!selected) return;
    const index = items.findIndex((item) => item.id === selected);
    const agent = items[index];
    const name = agent?.display_name || agent?.name || selected;
    if (!window.confirm(`Delete ${name}? This cannot be undone.`)) return;
    setDeleteError(null);
    try {
      await api.deleteAgent(worldId, selected);
      setSelectedId(items[index + 1]?.id ?? items[index - 1]?.id ?? null);
      agents.retry();
    } catch (error) {
      setDeleteError(error instanceof Error ? error.message : 'Unable to delete character.');
    }
  }

  if (!agents.data) {
    return (
      <PageState
        loading={agents.status === 'loading'}
        loadingLabel="Loading characters"
        error={agents.error}
        onRetry={agents.retry}
      />
    );
  }

  if (items.length === 0) {
    return (
      <main className={styles.page}>
        <section className={styles.empty}>
          <Users aria-hidden="true" />
          <h1>No characters yet</h1>
          <p>Create a character to begin defining a voice and identity.</p>
          <button type="button" onClick={() => setShowCreate(true)}>
            <Plus aria-hidden="true" />
            Create character
          </button>
        </section>
        {showCreate ? (
          <CreateAgentModal
            worldId={worldId}
            onClose={() => setShowCreate(false)}
            onCreated={agents.retry}
          />
        ) : null}
      </main>
    );
  }

  return (
    <main className={styles.workspace}>
      <aside className={styles.listPane}>
        <header>
          <div>
            <span>World cast</span>
            <h1>Characters</h1>
          </div>
          <button type="button" onClick={() => setShowCreate(true)} aria-label="Create character">
            <Plus aria-hidden="true" />
          </button>
        </header>
        <button
          className={styles.refresh}
          type="button"
          onClick={agents.retry}
          aria-label="Refresh characters"
        >
          <RefreshCw aria-hidden="true" />
          Refresh
        </button>
        <ResourceList
          items={items}
          selectedId={selected}
          getId={(item) => item.id}
          onSelect={setSelectedId}
          ariaLabel="Characters"
          renderItem={(item) => <CharacterListItem agent={item} />}
        />
        <footer>
          {items.length} {items.length === 1 ? 'character' : 'characters'}
        </footer>
      </aside>

      <section className={styles.detailPane}>
        {deleteError ? (
          <div className={styles.deleteError} role="alert">
            {deleteError}
          </div>
        ) : null}
        {selected ? (
          <AgentCardView
            worldId={worldId}
            agentId={selected}
            onClose={() => setSelectedId(null)}
            onDelete={deleteSelected}
          />
        ) : (
          <div className={styles.pick}>
            <Users aria-hidden="true" />
            <h2>Select a character</h2>
            <p>Choose a character to inspect its endpoint-backed profile and images.</p>
          </div>
        )}
      </section>

      <aside className={styles.contextPane}>
        {selected ? (
          <CharacterContext worldId={worldId} agentId={selected} agents={items} />
        ) : (
          <p className={styles.muted}>Character diaries and explicit relationships appear here.</p>
        )}
      </aside>
      {showCreate ? (
        <CreateAgentModal
          worldId={worldId}
          onClose={() => setShowCreate(false)}
          onCreated={agents.retry}
        />
      ) : null}
    </main>
  );
}

function CharacterListItem({ agent }: { agent: WorldAgent }) {
  const name = agent.display_name || agent.name;
  return (
    <div className={styles.listItem}>
      <AgentAvatar name={name} src={agent.avatar_url} size="sm" />
      <div>
        <strong>{name}</strong>
        {agent.kind ? <span>{agent.kind}</span> : null}
      </div>
    </div>
  );
}

function CharacterContext({
  worldId,
  agentId,
  agents,
}: {
  worldId: string;
  agentId: string;
  agents: WorldAgent[];
}) {
  const diaries = useResource(`character:${worldId}:${agentId}:diaries`, () =>
    api.fetchDiaries(worldId, agentId),
  );
  const relations = useResource(`character:${worldId}:${agentId}:relations`, () =>
    api.fetchRelations(worldId, agentId),
  );
  const memories = useResource(`character:${worldId}:${agentId}:memories`, () =>
    api.fetchMemorySummaries(worldId, agentId),
  );
  const voice = useResource(`character:${worldId}:${agentId}:voice`, () =>
    api.fetchAgentVoice(worldId, agentId),
  );
  const names = new Map(agents.map((agent) => [agent.id, agent.display_name || agent.name]));
  return (
    <div className={styles.contextStack}>
      <ContextSection
        title="Recent diaries"
        resource={diaries}
        retryLabel="Retry diaries"
        items={(data) => data.diaries}
        render={(item: DiaryEntry) => (
          <article key={item.id}>
            <p>{item.content}</p>
            {item.world_time ? <small>{item.world_time}</small> : null}
          </article>
        )}
        empty="No diary entries yet."
      />
      <ContextSection
        title="Relationships"
        resource={relations}
        retryLabel="Retry relationships"
        items={(data) => data.relations}
        render={(item: RelationEntry) => (
          <article key={`${item.agent_id}:${item.target_id}`}>
            <strong>{item.relation_type}</strong>
            <small>{names.get(item.target_id) ?? item.target_id}</small>
            {item.description ? <p>{item.description}</p> : null}
          </article>
        )}
        empty="No explicit relationships yet."
      />
      <ContextSection
        title="Memory summaries"
        resource={memories}
        retryLabel="Retry memory summaries"
        items={(data) => data.summaries}
        render={(item: MemorySummary) => (
          <article key={item.id}>
            <p>{item.summary}</p>
            <small>
              {item.period_start} - {item.period_end}
            </small>
          </article>
        )}
        empty="No memory summaries yet."
      />
      <VoiceSection resource={voice} />
    </div>
  );
}

function VoiceSection({
  resource,
}: {
  resource: ReturnType<typeof useResource<{ ok: boolean; voice: VoiceFingerprint }>>;
}) {
  const voice = resource.data?.voice;
  return (
    <section className={styles.contextSection}>
      <h2>Voice fingerprint</h2>
      {resource.status === 'loading' && !voice ? <p className={styles.muted}>Loading...</p> : null}
      {resource.error && !voice ? (
        <div className={styles.contextError} role="alert">
          <span>{resource.error.message}</span>
          <button type="button" onClick={resource.retry} aria-label="Retry voice fingerprint">
            Retry
          </button>
        </div>
      ) : null}
      {voice ? (
        <article>
          <dl className={styles.voiceMetrics}>
            <div>
              <dt>Samples</dt>
              <dd>{voice.sample_count}</dd>
            </div>
            <div>
              <dt>Average sentence length</dt>
              <dd>{voice.avg_sentence_length}</dd>
            </div>
          </dl>
          {voice.signature_words.length ? (
            <div className={styles.voiceWords}>
              {voice.signature_words.map((word) => (
                <span key={word}>{word}</span>
              ))}
            </div>
          ) : null}
        </article>
      ) : null}
    </section>
  );
}

function ContextSection<TData, TItem>({
  title,
  resource,
  retryLabel,
  items,
  render,
  empty,
}: {
  title: string;
  resource: ReturnType<typeof useResource<TData>>;
  retryLabel: string;
  items(data: TData): TItem[];
  render(item: TItem): React.ReactNode;
  empty: string;
}) {
  return (
    <section className={styles.contextSection}>
      <h2>{title}</h2>
      {resource.status === 'loading' && !resource.data ? (
        <p className={styles.muted}>Loading...</p>
      ) : null}
      {resource.error && !resource.data ? (
        <div className={styles.contextError} role="alert">
          <span>{resource.error.message}</span>
          <button type="button" onClick={resource.retry} aria-label={retryLabel}>
            Retry
          </button>
        </div>
      ) : null}
      {resource.data ? (
        items(resource.data).length ? (
          items(resource.data).slice(0, 8).map(render)
        ) : (
          <p className={styles.muted}>{empty}</p>
        )
      ) : null}
    </section>
  );
}
