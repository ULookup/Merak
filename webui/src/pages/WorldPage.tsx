import { BookOpen, Building2, Clock3, Globe2, Network, Users } from 'lucide-react';
import { api } from '../api/client';
import type { GraphEntity, KnowledgeRecord, TimelineEventRecord } from '../api/types';
import { worldbuildingApi } from '../api/worldbuilding';
import PageState from '../components/layout/PageState';
import { useResource, type ResourceState } from '../hooks/useResource';
import styles from './WorldPage.module.css';

interface WorldPageProps {
  worldId: string;
}

function SectionState<T>({
  resource,
  retryLabel,
  children,
}: {
  resource: ResourceState<T>;
  retryLabel: string;
  children: (data: T) => React.ReactNode;
}) {
  if (resource.data) return <>{children(resource.data)}</>;
  if (resource.status === 'error') {
    return (
      <div className={styles.inlineError} role="alert">
        <span>{resource.error?.message}</span>
        <button type="button" onClick={resource.retry} aria-label={retryLabel}>
          Retry
        </button>
      </div>
    );
  }
  return <div className={styles.loading}>Loading...</div>;
}

export default function WorldPage({ worldId }: WorldPageProps) {
  const detail = useResource(`world:${worldId}`, () => api.getWorldDetail(worldId));
  const locations = useResource(`world:${worldId}:locations`, () =>
    worldbuildingApi.listLocations(worldId),
  );
  const factions = useResource(`world:${worldId}:factions`, () =>
    worldbuildingApi.listFactions(worldId),
  );
  const knowledge = useResource(`world:${worldId}:knowledge`, () =>
    worldbuildingApi.listKnowledge(worldId),
  );
  const timeline = useResource(`world:${worldId}:timeline`, () =>
    worldbuildingApi.getTimeline(worldId),
  );
  const graph = useResource(`world:${worldId}:graph`, () =>
    worldbuildingApi.listGraphEntities(worldId),
  );

  if (!detail.data) {
    return (
      <PageState
        loading={detail.status === 'loading'}
        loadingLabel="Loading world"
        error={detail.error}
        onRetry={detail.retry}
      />
    );
  }

  const world = detail.data.world;
  return (
    <main className={styles.page}>
      <header className={styles.heading}>
        <div className={styles.titleIcon}>
          <Globe2 aria-hidden="true" />
        </div>
        <div>
          <span>World setting</span>
          <h1>{world.name}</h1>
          {world.description ? <p>{world.description}</p> : null}
        </div>
      </header>

      <section className={styles.stats} aria-label="World totals">
        <Stat label="Characters" value={world.stats.agents} icon={Users} />
        <Stat label="Chapters" value={world.stats.chapters} icon={BookOpen} />
        <Stat label="Scenes" value={world.stats.scenes} icon={Clock3} />
      </section>

      <div className={styles.grid}>
        <Panel title="Locations" icon={Building2}>
          <SectionState resource={locations} retryLabel="Retry locations">
            {(data) => {
              const items = data.items ?? [];
              return items.length ? (
                <div className={styles.cards}>
                  {items.map((item) => (
                    <article key={item.id}>
                      <strong>{item.name}</strong>
                      {item.description ? <p>{item.description}</p> : null}
                    </article>
                  ))}
                </div>
              ) : (
                <Empty text="No locations yet." />
              );
            }}
          </SectionState>
        </Panel>
        <Panel title="Factions" icon={Users}>
          <SectionState resource={factions} retryLabel="Retry factions">
            {(data) => {
              const items = data.items ?? [];
              return items.length ? (
                <div className={styles.cards}>
                  {items.map((item) => (
                    <article key={item.id}>
                      <strong>{item.name}</strong>
                      {item.description ? <p>{item.description}</p> : null}
                    </article>
                  ))}
                </div>
              ) : (
                <Empty text="No factions yet." />
              );
            }}
          </SectionState>
        </Panel>
        <Panel title="Knowledge" icon={BookOpen}>
          <SectionState resource={knowledge} retryLabel="Retry knowledge">
            {(data) => <KnowledgeList items={data.items ?? []} />}
          </SectionState>
        </Panel>
        <Panel title="Timeline" icon={Clock3}>
          <SectionState resource={timeline} retryLabel="Retry timeline">
            {(data) => <Timeline items={data.items ?? []} currentTime={data.current_time?.label} />}
          </SectionState>
        </Panel>
        <Panel title="Knowledge graph" icon={Network} wide>
          <SectionState resource={graph} retryLabel="Retry graph">
            {(data) => <GraphList items={data.items ?? []} />}
          </SectionState>
        </Panel>
      </div>
    </main>
  );
}

function Stat({ label, value, icon: Icon }: { label: string; value: number; icon: typeof Users }) {
  return (
    <article>
      <Icon aria-hidden="true" />
      <div>
        <strong>{value}</strong>
        <span>{label}</span>
      </div>
    </article>
  );
}
function Panel({
  title,
  icon: Icon,
  wide = false,
  children,
}: {
  title: string;
  icon: typeof Users;
  wide?: boolean;
  children: React.ReactNode;
}) {
  return (
    <section className={`${styles.panel} ${wide ? styles.wide : ''}`}>
      <header>
        <Icon aria-hidden="true" />
        <h2>{title}</h2>
      </header>
      {children}
    </section>
  );
}
function Empty({ text }: { text: string }) {
  return <p className={styles.empty}>{text}</p>;
}
function KnowledgeList({ items }: { items: KnowledgeRecord[] }) {
  return items.length ? (
    <div className={styles.rows}>
      {items.map((item) => (
        <article key={item.id}>
          <strong>{item.category}</strong>
          <p>{item.content}</p>
          {item.tags?.length ? <small>{item.tags.join(' · ')}</small> : null}
        </article>
      ))}
    </div>
  ) : (
    <Empty text="No knowledge records yet." />
  );
}
function Timeline({ items, currentTime }: { items: TimelineEventRecord[]; currentTime?: string }) {
  return (
    <>
      {currentTime ? <div className={styles.currentTime}>{currentTime}</div> : null}
      {items.length ? (
        <div className={styles.timeline}>
          {items.map((item) => (
            <article key={item.id}>
              <strong>{item.world_time}</strong>
              <p>{item.description}</p>
            </article>
          ))}
        </div>
      ) : (
        <Empty text="No timeline events yet." />
      )}
    </>
  );
}
function GraphList({ items }: { items: GraphEntity[] }) {
  return items.length ? (
    <div className={styles.entityList}>
      {items.map((item) => (
        <span key={item.id}>
          <small>{item.type}</small>
          {item.name}
        </span>
      ))}
    </div>
  ) : (
    <Empty text="No graph entities yet." />
  );
}
