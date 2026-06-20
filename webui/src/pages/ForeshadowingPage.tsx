import { useEffect, useMemo, useRef, useState } from 'react';
import { Filter, Pencil, Plus, RefreshCw, Sparkles, Trash2 } from 'lucide-react';
import { api } from '../api/client';
import type { ForeshadowingItem, StoryChapter, StoryScene } from '../api/types';
import CreateForeshadowingModal from '../components/Inspector/CreateForeshadowingModal';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import styles from './ForeshadowingPage.module.css';

type Data = {
  items: ForeshadowingItem[];
  chapters: StoryChapter[];
  scenes: StoryScene[];
  contextError: string | null;
};

export function deriveForeshadowingStatus(
  positions: { status?: string; plannedPosition: number | null; currentPosition: number | null },
): 'overdue' | null {
  if (positions.status !== 'open' || positions.plannedPosition == null || positions.currentPosition == null) return null;
  return positions.currentPosition > positions.plannedPosition ? 'overdue' : null;
}

export default function ForeshadowingPage({ worldId }: { worldId: string }) {
  const resource = useResource(`foreshadowing:${worldId}`, async () => {
    const [foreshadowing, chapters, scenes] = await Promise.allSettled([
      api.listForeshadowing(worldId), api.listChapters(worldId), api.listScenes(worldId),
    ]);
    if (foreshadowing.status === 'rejected') throw foreshadowing.reason;
    const failures = [chapters, scenes].filter((result) => result.status === 'rejected') as PromiseRejectedResult[];
    return {
      items: foreshadowing.value.foreshadowing ?? foreshadowing.value.items ?? [],
      chapters: chapters.status === 'fulfilled' ? chapters.value.chapters : [],
      scenes: scenes.status === 'fulfilled' ? scenes.value.scenes : [],
      contextError: failures.map((result) => result.reason instanceof Error ? result.reason.message : String(result.reason)).join(' ') || null,
    };
  });
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [filter, setFilter] = useState('all');
  const [createOpen, setCreateOpen] = useState(false);
  const [editing, setEditing] = useState(false);
  const [payoff, setPayoff] = useState('');
  const [removed, setRemoved] = useState<string[]>([]);
  const [mutationError, setMutationError] = useState<string | null>(null);
  const [pendingMutation, setPendingMutation] = useState<string | null>(null);
  const pendingMutationRef = useRef(false);
  const generationRef = useRef(0);
  const renderedWorldRef = useRef(worldId);
  if (renderedWorldRef.current !== worldId) {
    renderedWorldRef.current = worldId;
    generationRef.current += 1;
  }
  const data: Data | null = resource.data;
  const items = useMemo(() => (data?.items ?? []).filter((item) => !removed.includes(item.id)), [data, removed]);
  const filtered = filter === 'all' ? items : items.filter((item) => item.status === filter);
  const selected = filtered.find((item) => item.id === selectedId) ?? null;

  useEffect(() => { setSelectedId(null); setRemoved([]); setEditing(false); setCreateOpen(false); setPendingMutation(null); pendingMutationRef.current = false; setMutationError(null); }, [worldId]);
  useEffect(() => () => { generationRef.current += 1; }, []);
  useEffect(() => {
    if (selectedId && !filtered.some((item) => item.id === selectedId)) setSelectedId(filtered[0]?.id ?? null);
  }, [filtered, selectedId]);
  useEffect(() => { setPayoff(selected?.pay_off_idea ?? ''); setEditing(false); }, [selected?.id]);

  async function save() {
    if (!selected || pendingMutationRef.current || !window.confirm('Update this foreshadowing item?')) return;
    const operationWorld = worldId;
    const operationGeneration = generationRef.current;
    pendingMutationRef.current = true;
    setPendingMutation('update');
    setMutationError(null);
    try { await api.patchForeshadow(operationWorld, selected.id, { pay_off_idea: payoff }); if (generationRef.current === operationGeneration) { setEditing(false); resource.retry(); } }
    catch (error) { if (generationRef.current === operationGeneration) setMutationError((error as Error).message); }
    finally { if (generationRef.current === operationGeneration) { pendingMutationRef.current = false; setPendingMutation(null); } }
  }

  async function remove() {
    if (!selected || pendingMutationRef.current || !window.confirm('Delete this foreshadowing item?')) return;
    setMutationError(null);
    const id = selected.id;
    const operationWorld = worldId;
    const operationGeneration = generationRef.current;
    pendingMutationRef.current = true;
    setPendingMutation('delete');
    try { await api.deleteForeshadowing(operationWorld, id); if (generationRef.current === operationGeneration) { setRemoved((ids) => [...ids, id]); setSelectedId(filtered.find((item) => item.id !== id)?.id ?? null); resource.retry(); } }
    catch (error) { if (generationRef.current === operationGeneration) setMutationError((error as Error).message); }
    finally { if (generationRef.current === operationGeneration) { pendingMutationRef.current = false; setPendingMutation(null); } }
  }

  function changeFilter(nextFilter: string) {
    setFilter(nextFilter);
    const visible = nextFilter === 'all' ? items : items.filter((item) => item.status === nextFilter);
    setSelectedId(visible[0]?.id ?? null);
  }

  if (!data) return <PageState loading={resource.status === 'loading'} loadingLabel="Loading foreshadowing" error={resource.error} onRetry={resource.retry} />;
  return (
    <main className={styles.workspace}>
      <aside className={styles.listPane}>
        <header><div><span>Narrative threads</span><h1>Foreshadowing</h1></div><div className={styles.tools}><button aria-label="Refresh foreshadowing" onClick={resource.retry}><RefreshCw /></button><button aria-label="Create foreshadowing" disabled={Boolean(pendingMutation)} onClick={() => setCreateOpen(true)}><Plus /></button></div></header>
        <label className={styles.filter}><Filter /><span>Status</span><select value={filter} onChange={(event) => changeFilter(event.target.value)}><option value="all">All</option><option value="open">Open</option><option value="paid">Paid</option><option value="abandoned">Abandoned</option></select></label>
        {(resource.error || mutationError || data.contextError) && <div role="alert" className={styles.warning}>{mutationError ?? resource.error?.message ?? data.contextError}</div>}
        {filtered.length ? <ResourceList items={filtered} selectedId={selectedId} getId={(item) => item.id} onSelect={setSelectedId} ariaLabel="Foreshadowing" renderItem={(item) => <div className={styles.listItem}><Sparkles /><div><strong>{item.content}</strong><span>{item.status ?? 'No status'}</span></div></div>} /> : <p className={styles.empty}>No foreshadowing matches this filter.</p>}
        <footer>{filtered.length} {filtered.length === 1 ? 'thread' : 'threads'}</footer>
      </aside>
      <section className={styles.detail} aria-label="Foreshadowing detail">
        {selected ? <><header className={styles.detailHeader}><div><span>Thread detail</span><h2>{selected.content}</h2></div><div className={styles.actions}><button aria-label="Edit foreshadowing" disabled={Boolean(pendingMutation)} onClick={() => setEditing(true)}><Pencil /> Edit</button><button aria-label="Delete foreshadowing" disabled={Boolean(pendingMutation)} onClick={remove}><Trash2 /> Delete</button></div></header>
          <div className={styles.cards}><Fact label="Status" value={selected.status} /><Fact label="Hint level" value={selected.hint_level} /><Fact label="Planted at" value={selected.planted_at} /><Fact label="Paid at" value={selected.paid_at} /></div>
          <section className={styles.block}><h3>Payoff idea</h3>{editing ? <><textarea aria-label="Payoff idea" disabled={Boolean(pendingMutation)} value={payoff} onChange={(event) => setPayoff(event.target.value)} /><button className={styles.primary} disabled={Boolean(pendingMutation)} onClick={save}>Save changes</button></> : <p>{selected.pay_off_idea || 'No payoff idea recorded.'}</p>}</section>
          {selected.tags?.length ? <section className={styles.block}><h3>Tags</h3><div className={styles.chips}>{selected.tags.map((tag) => <span key={tag}>{tag}</span>)}</div></section> : null}
        </> : <Pick icon={<Sparkles />} title="Select foreshadowing" text="Choose a thread to inspect its recorded details." />}
      </section>
      <aside className={styles.contextPane}><h2>Real context</h2>{selected ? <ForeshadowingContext item={selected} data={data} /> : <p>Select a thread to see resolved narrative positions.</p>}</aside>
      {createOpen ? <CreateForeshadowingModal worldId={worldId} onClose={() => setCreateOpen(false)} onCreated={resource.retry} /> : null}
    </main>
  );
}

function Fact({ label, value }: { label: string; value?: string }) { return <div className={styles.fact}><span>{label}</span><strong>{value || 'Not set'}</strong></div>; }
function Pick({ icon, title, text }: { icon: React.ReactNode; title: string; text: string }) { return <div className={styles.pick}>{icon}<h2>{title}</h2><p>{text}</p></div>; }
function ForeshadowingContext({ item, data }: { item: ForeshadowingItem; data: Data }) {
  const resolve = (id?: string) => data.scenes.find((scene) => scene.id === id)?.title ?? data.chapters.find((chapter) => chapter.id === id)?.title;
  const planted = resolve(item.planted_at);
  const paid = resolve(item.paid_at);
  return <div className={styles.context}>{planted ? <Fact label="Planted narrative position" value={planted} /> : null}{paid ? <Fact label="Paid narrative position" value={paid} /> : null}{!planted && !paid ? <p>No resolvable narrative positions recorded.</p> : null}</div>;
}
