import { useEffect, useMemo, useState } from 'react';
import { Filter, Pencil, Plus, RefreshCw, Sparkles, Trash2 } from 'lucide-react';
import { api } from '../api/client';
import type { ForeshadowingItem, StoryChapter, StoryScene, WorldAgent } from '../api/types';
import CreateForeshadowingModal from '../components/Inspector/CreateForeshadowingModal';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import styles from './ForeshadowingPage.module.css';

type Data = {
  items: ForeshadowingItem[];
  chapters: StoryChapter[];
  scenes: StoryScene[];
  agents: WorldAgent[];
  contextError: string | null;
};

export function deriveForeshadowingStatus(
  item: Pick<ForeshadowingItem, 'status' | 'planned_chapter_position' | 'current_chapter_position'>,
  _currentChapterPosition: number | null,
): 'overdue' | null {
  if (
    item.status !== 'open' ||
    item.planned_chapter_position == null ||
    item.current_chapter_position == null
  ) return null;
  return item.current_chapter_position > item.planned_chapter_position ? 'overdue' : null;
}

export default function ForeshadowingPage({ worldId }: { worldId: string }) {
  const resource = useResource(`foreshadowing:${worldId}`, async () => {
    const [foreshadowing, chapters, scenes, agents] = await Promise.allSettled([
      api.listForeshadowing(worldId), api.listChapters(worldId), api.listScenes(worldId), api.listAgents(worldId),
    ]);
    if (foreshadowing.status === 'rejected') throw foreshadowing.reason;
    const failures = [chapters, scenes, agents].filter((result) => result.status === 'rejected') as PromiseRejectedResult[];
    return {
      items: foreshadowing.value.foreshadowing ?? foreshadowing.value.items ?? [],
      chapters: chapters.status === 'fulfilled' ? chapters.value.chapters : [],
      scenes: scenes.status === 'fulfilled' ? scenes.value.scenes : [],
      agents: agents.status === 'fulfilled' ? agents.value.agents : [],
      contextError: failures.length ? failures.map((result) => result.reason instanceof Error ? result.reason.message : String(result.reason)).join(' ') : null,
    };
  });
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [filter, setFilter] = useState('all');
  const [createOpen, setCreateOpen] = useState(false);
  const [editing, setEditing] = useState(false);
  const [payoff, setPayoff] = useState('');
  const [removed, setRemoved] = useState<string[]>([]);
  const [mutationError, setMutationError] = useState<string | null>(null);
  const data: Data | null = resource.data;
  const items = useMemo(() => (data?.items ?? []).filter((item) => !removed.includes(item.id)), [data, removed]);
  const filtered = filter === 'all' ? items : items.filter((item) => item.status === filter);
  const selected = items.find((item) => item.id === selectedId) ?? null;

  useEffect(() => { setSelectedId(null); setRemoved([]); setEditing(false); }, [worldId]);
  useEffect(() => {
    if (selectedId && !items.some((item) => item.id === selectedId)) setSelectedId(items[0]?.id ?? null);
  }, [items, selectedId]);
  useEffect(() => { setPayoff(selected?.pay_off_idea ?? ''); setEditing(false); }, [selected?.id]);

  async function save() {
    if (!selected || !window.confirm('Update this foreshadowing item?')) return;
    setMutationError(null);
    try { await api.patchForeshadow(worldId, selected.id, { pay_off_idea: payoff }); setEditing(false); resource.retry(); }
    catch (error) { setMutationError((error as Error).message); }
  }

  async function remove() {
    if (!selected || !window.confirm('Delete this foreshadowing item?')) return;
    setMutationError(null);
    const id = selected.id;
    try { await api.deleteForeshadowing(worldId, id); setRemoved((ids) => [...ids, id]); setSelectedId(items.find((item) => item.id !== id)?.id ?? null); resource.retry(); }
    catch (error) { setMutationError((error as Error).message); }
  }

  if (!data) return <PageState loading={resource.status === 'loading'} loadingLabel="Loading foreshadowing" error={resource.error} onRetry={resource.retry} />;
  return (
    <main className={styles.workspace}>
      <aside className={styles.listPane}>
        <header><div><span>Narrative threads</span><h1>Foreshadowing</h1></div><div className={styles.tools}><button aria-label="Refresh foreshadowing" onClick={resource.retry}><RefreshCw /></button><button aria-label="Create foreshadowing" onClick={() => setCreateOpen(true)}><Plus /></button></div></header>
        <label className={styles.filter}><Filter /><span>Status</span><select value={filter} onChange={(event) => setFilter(event.target.value)}><option value="all">All</option><option value="open">Open</option><option value="paid">Paid</option><option value="abandoned">Abandoned</option></select></label>
        {(resource.error || mutationError || data.contextError) && <div role="alert" className={styles.warning}>{mutationError ?? resource.error?.message ?? data.contextError}</div>}
        {filtered.length ? <ResourceList items={filtered} selectedId={selectedId} getId={(item) => item.id} onSelect={setSelectedId} ariaLabel="Foreshadowing" renderItem={(item) => <div className={styles.listItem}><Sparkles /><div><strong>{item.content}</strong><span>{item.status ?? 'No status'}{deriveForeshadowingStatus(item, null) ? ' · overdue' : ''}</span></div></div>} /> : <p className={styles.empty}>No foreshadowing matches this filter.</p>}
        <footer>{filtered.length} {filtered.length === 1 ? 'thread' : 'threads'}</footer>
      </aside>
      <section className={styles.detail} aria-label="Foreshadowing detail">
        {selected ? <><header className={styles.detailHeader}><div><span>Thread detail</span><h2>{selected.content}</h2></div><div className={styles.actions}><button aria-label="Edit foreshadowing" onClick={() => setEditing(true)}><Pencil /> Edit</button><button aria-label="Delete foreshadowing" onClick={remove}><Trash2 /> Delete</button></div></header>
          <div className={styles.cards}><Fact label="Status" value={selected.status} /><Fact label="Hint level" value={selected.hint_level} /><Fact label="Planted at" value={selected.planted_at} /><Fact label="Paid at" value={selected.paid_at} /></div>
          <section className={styles.block}><h3>Payoff idea</h3>{editing ? <><textarea aria-label="Payoff idea" value={payoff} onChange={(event) => setPayoff(event.target.value)} /><button className={styles.primary} onClick={save}>Save changes</button></> : <p>{selected.pay_off_idea || 'No payoff idea recorded.'}</p>}</section>
          {selected.tags?.length ? <section className={styles.block}><h3>Tags</h3><div className={styles.chips}>{selected.tags.map((tag) => <span key={tag}>{tag}</span>)}</div></section> : null}
        </> : <Pick icon={<Sparkles />} title="Select foreshadowing" text="Choose a thread to inspect its recorded details." />}
      </section>
      <aside className={styles.contextPane}><h2>Real context</h2>{selected ? <Context item={selected} data={data} /> : <p>Select a thread to see resolved relationships.</p>}</aside>
      {createOpen ? <CreateForeshadowingModal worldId={worldId} onClose={() => setCreateOpen(false)} onCreated={resource.retry} /> : null}
    </main>
  );
}

function Fact({ label, value }: { label: string; value?: string }) { return <div className={styles.fact}><span>{label}</span><strong>{value || 'Not set'}</strong></div>; }
function Pick({ icon, title, text }: { icon: React.ReactNode; title: string; text: string }) { return <div className={styles.pick}>{icon}<h2>{title}</h2><p>{text}</p></div>; }
function Context({ item, data }: { item: ForeshadowingItem; data: Data }) {
  const chapter = data.chapters.find((entry) => entry.id === item.chapter_id);
  const scene = data.scenes.find((entry) => entry.id === item.scene_id);
  const agents = (item.related_agent_ids ?? []).map((id) => data.agents.find((entry) => entry.id === id)).filter(Boolean) as WorldAgent[];
  return <div className={styles.context}>{chapter && <Fact label="Chapter" value={chapter.title} />}{scene && <Fact label="Scene" value={scene.title} />}{agents.length ? <div><h3>Characters</h3><div className={styles.chips}>{agents.map((agent) => <span key={agent.id}>{agent.display_name || agent.name}</span>)}</div></div> : null}{!chapter && !scene && !agents.length ? <p>No resolvable relationships recorded.</p> : null}</div>;
}
