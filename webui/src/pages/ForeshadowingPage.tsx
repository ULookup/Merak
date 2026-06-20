import { useEffect, useMemo, useRef, useState } from 'react';
import { Filter, Pencil, Plus, RefreshCw, Sparkles, Trash2 } from 'lucide-react';
import { api } from '../api/client';
import type { ForeshadowingItem } from '../api/types';
import CreateForeshadowingModal from '../components/Inspector/CreateForeshadowingModal';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import styles from './ForeshadowingPage.module.css';

type Data = {
  items: ForeshadowingItem[];
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
    const foreshadowing = await api.listForeshadowing(worldId);
    return { items: foreshadowing.foreshadowing ?? foreshadowing.items ?? [] };
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
  const worldRef = useRef(worldId);
  worldRef.current = worldId;
  const data: Data | null = resource.data;
  const items = useMemo(() => (data?.items ?? []).filter((item) => !removed.includes(item.id)), [data, removed]);
  const filtered = filter === 'all' ? items : items.filter((item) => item.status === filter);
  const selected = filtered.find((item) => item.id === selectedId) ?? null;

  useEffect(() => { setSelectedId(null); setRemoved([]); setEditing(false); setCreateOpen(false); setPendingMutation(null); pendingMutationRef.current = false; setMutationError(null); }, [worldId]);
  useEffect(() => {
    if (selectedId && !filtered.some((item) => item.id === selectedId)) setSelectedId(filtered[0]?.id ?? null);
  }, [filtered, selectedId]);
  useEffect(() => { setPayoff(selected?.pay_off_idea ?? ''); setEditing(false); }, [selected?.id]);

  async function save() {
    if (!selected || pendingMutationRef.current || !window.confirm('Update this foreshadowing item?')) return;
    const operationWorld = worldId;
    pendingMutationRef.current = true;
    setPendingMutation('update');
    setMutationError(null);
    try { await api.patchForeshadow(operationWorld, selected.id, { pay_off_idea: payoff }); if (worldRef.current === operationWorld) { setEditing(false); resource.retry(); } }
    catch (error) { if (worldRef.current === operationWorld) setMutationError((error as Error).message); }
    finally { if (worldRef.current === operationWorld) { pendingMutationRef.current = false; setPendingMutation(null); } }
  }

  async function remove() {
    if (!selected || pendingMutationRef.current || !window.confirm('Delete this foreshadowing item?')) return;
    setMutationError(null);
    const id = selected.id;
    const operationWorld = worldId;
    pendingMutationRef.current = true;
    setPendingMutation('delete');
    try { await api.deleteForeshadowing(operationWorld, id); if (worldRef.current === operationWorld) { setRemoved((ids) => [...ids, id]); setSelectedId(filtered.find((item) => item.id !== id)?.id ?? null); resource.retry(); } }
    catch (error) { if (worldRef.current === operationWorld) setMutationError((error as Error).message); }
    finally { if (worldRef.current === operationWorld) { pendingMutationRef.current = false; setPendingMutation(null); } }
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
        {(resource.error || mutationError) && <div role="alert" className={styles.warning}>{mutationError ?? resource.error?.message}</div>}
        {filtered.length ? <ResourceList items={filtered} selectedId={selectedId} getId={(item) => item.id} onSelect={setSelectedId} ariaLabel="Foreshadowing" renderItem={(item) => <div className={styles.listItem}><Sparkles /><div><strong>{item.content}</strong><span>{item.status ?? 'No status'}{deriveForeshadowingStatus(item, null) ? ' · overdue' : ''}</span></div></div>} /> : <p className={styles.empty}>No foreshadowing matches this filter.</p>}
        <footer>{filtered.length} {filtered.length === 1 ? 'thread' : 'threads'}</footer>
      </aside>
      <section className={styles.detail} aria-label="Foreshadowing detail">
        {selected ? <><header className={styles.detailHeader}><div><span>Thread detail</span><h2>{selected.content}</h2></div><div className={styles.actions}><button aria-label="Edit foreshadowing" disabled={Boolean(pendingMutation)} onClick={() => setEditing(true)}><Pencil /> Edit</button><button aria-label="Delete foreshadowing" disabled={Boolean(pendingMutation)} onClick={remove}><Trash2 /> Delete</button></div></header>
          <div className={styles.cards}><Fact label="Status" value={selected.status} /><Fact label="Hint level" value={selected.hint_level} /><Fact label="Planted at" value={selected.planted_at} /><Fact label="Paid at" value={selected.paid_at} /></div>
          <section className={styles.block}><h3>Payoff idea</h3>{editing ? <><textarea aria-label="Payoff idea" disabled={Boolean(pendingMutation)} value={payoff} onChange={(event) => setPayoff(event.target.value)} /><button className={styles.primary} disabled={Boolean(pendingMutation)} onClick={save}>Save changes</button></> : <p>{selected.pay_off_idea || 'No payoff idea recorded.'}</p>}</section>
          {selected.tags?.length ? <section className={styles.block}><h3>Tags</h3><div className={styles.chips}>{selected.tags.map((tag) => <span key={tag}>{tag}</span>)}</div></section> : null}
        </> : <Pick icon={<Sparkles />} title="Select foreshadowing" text="Choose a thread to inspect its recorded details." />}
      </section>
      <aside className={styles.contextPane}><h2>Real context</h2><p>The foreshadowing list API does not expose chapter, scene, or character relationships.</p></aside>
      {createOpen ? <CreateForeshadowingModal worldId={worldId} onClose={() => setCreateOpen(false)} onCreated={resource.retry} /> : null}
    </main>
  );
}

function Fact({ label, value }: { label: string; value?: string }) { return <div className={styles.fact}><span>{label}</span><strong>{value || 'Not set'}</strong></div>; }
function Pick({ icon, title, text }: { icon: React.ReactNode; title: string; text: string }) { return <div className={styles.pick}>{icon}<h2>{title}</h2><p>{text}</p></div>; }
