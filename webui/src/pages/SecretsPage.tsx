import { useEffect, useMemo, useState } from 'react';
import { Eye, Filter, KeyRound, Pencil, Plus, RefreshCw, Trash2 } from 'lucide-react';
import { api } from '../api/client';
import type { ForeshadowingItem, SecretItem, StoryChapter, StoryScene, WorldAgent } from '../api/types';
import CreateSecretModal from '../components/Inspector/CreateSecretModal';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import styles from './SecretsPage.module.css';

type Data = { items: SecretItem[]; foreshadowing: ForeshadowingItem[]; chapters: StoryChapter[]; scenes: StoryScene[]; agents: WorldAgent[]; contextError: string | null };

export default function SecretsPage({ worldId }: { worldId: string }) {
  const resource = useResource(`secrets:${worldId}`, async () => {
    const [secrets, foreshadowing, chapters, scenes, agents] = await Promise.allSettled([api.listSecrets(worldId), api.listForeshadowing(worldId), api.listChapters(worldId), api.listScenes(worldId), api.listAgents(worldId)]);
    if (secrets.status === 'rejected') throw secrets.reason;
    const failures = [foreshadowing, chapters, scenes, agents].filter((result) => result.status === 'rejected') as PromiseRejectedResult[];
    return {
      items: secrets.value.secrets ?? secrets.value.items ?? [],
      foreshadowing: foreshadowing.status === 'fulfilled' ? foreshadowing.value.foreshadowing ?? foreshadowing.value.items ?? [] : [],
      chapters: chapters.status === 'fulfilled' ? chapters.value.chapters : [],
      scenes: scenes.status === 'fulfilled' ? scenes.value.scenes : [],
      agents: agents.status === 'fulfilled' ? agents.value.agents : [],
      contextError: failures.length ? failures.map((result) => result.reason instanceof Error ? result.reason.message : String(result.reason)).join(' ') : null,
    };
  });
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [filter, setFilter] = useState('all');
  const [revealedTruth, setRevealedTruth] = useState<string | null>(null);
  const [createOpen, setCreateOpen] = useState(false);
  const [editing, setEditing] = useState(false);
  const [publicVersion, setPublicVersion] = useState('');
  const [removed, setRemoved] = useState<string[]>([]);
  const [mutationError, setMutationError] = useState<string | null>(null);
  const data: Data | null = resource.data;
  const items = useMemo(() => (data?.items ?? []).filter((item) => !removed.includes(item.id)), [data, removed]);
  const filtered = filter === 'all' ? items : items.filter((item) => item.status === filter);
  const selected = items.find((item) => item.id === selectedId) ?? null;

  useEffect(() => { setSelectedId(null); setRemoved([]); setRevealedTruth(null); setEditing(false); }, [worldId]);
  useEffect(() => { setRevealedTruth(null); setEditing(false); setPublicVersion(selected?.public_version ?? ''); }, [selected?.id]);
  useEffect(() => { if (selectedId && !items.some((item) => item.id === selectedId)) setSelectedId(items[0]?.id ?? null); }, [items, selectedId]);

  async function save() { if (!selected || !window.confirm('Update this secret?')) return; setMutationError(null); try { await api.patchSecret(worldId, selected.id, { public_version: publicVersion }); setEditing(false); resource.retry(); } catch (error) { setMutationError((error as Error).message); } }
  async function remove() { if (!selected || !window.confirm('Delete this secret?')) return; setMutationError(null); const id = selected.id; try { await api.deleteSecret(worldId, id); setRemoved((ids) => [...ids, id]); setSelectedId(items.find((item) => item.id !== id)?.id ?? null); setRevealedTruth(null); resource.retry(); } catch (error) { setMutationError((error as Error).message); } }

  if (!data) return <PageState loading={resource.status === 'loading'} loadingLabel="Loading secrets" error={resource.error} onRetry={resource.retry} />;
  return <main className={styles.workspace}>
    <aside className={styles.listPane}><header><div><span>Knowledge boundaries</span><h1>Secrets</h1></div><div className={styles.tools}><button aria-label="Refresh secrets" onClick={resource.retry}><RefreshCw /></button><button aria-label="Create secret" onClick={() => setCreateOpen(true)}><Plus /></button></div></header>
      <label className={styles.filter}><Filter /><span>Status</span><select value={filter} onChange={(event) => setFilter(event.target.value)}><option value="all">All</option><option value="active">Active</option><option value="revealed">Revealed</option><option value="abandoned">Abandoned</option></select></label>
      {(resource.error || mutationError || data.contextError) && <div role="alert" className={styles.warning}>{mutationError ?? resource.error?.message ?? data.contextError}</div>}
      {filtered.length ? <ResourceList items={filtered} selectedId={selectedId} getId={(item) => item.id} onSelect={setSelectedId} ariaLabel="Secrets" renderItem={(item) => <div className={styles.listItem}><KeyRound /><div><strong>{item.title || item.public_version || 'Untitled secret'}</strong><span>{item.status ?? 'No status'}</span></div></div>} /> : <p className={styles.empty}>No secrets match this filter.</p>}
      <footer>{filtered.length} {filtered.length === 1 ? 'secret' : 'secrets'}</footer></aside>
    <section className={styles.detail} aria-label="Secret detail">{selected ? <><header className={styles.detailHeader}><div><span>Secret detail</span><h2>{selected.title || selected.public_version || 'Untitled secret'}</h2></div><div className={styles.actions}><button aria-label="Edit secret" onClick={() => setEditing(true)}><Pencil /> Edit</button><button aria-label="Delete secret" onClick={remove}><Trash2 /> Delete</button></div></header>
      <div className={styles.cards}><Fact label="Status" value={selected.status} /><Fact label="Stakes" value={selected.stakes} /></div>
      <section className={styles.block}><h3>Public version</h3>{editing ? <><textarea aria-label="Public version" value={publicVersion} onChange={(event) => setPublicVersion(event.target.value)} /><button className={styles.primary} onClick={save}>Save changes</button></> : <p>{selected.public_version || 'No public version recorded.'}</p>}</section>
      <section className={styles.truth}><h3>Truth</h3>{revealedTruth === null ? <div><KeyRound /><p>Hidden until you explicitly reveal it.</p><button aria-label="Reveal truth" onClick={() => setRevealedTruth(selected.truth ?? selected.content ?? '')}><Eye /> Reveal truth</button></div> : <p>{revealedTruth || 'No truth recorded.'}</p>}</section>
    </> : <div className={styles.pick}><KeyRound /><h2>Select a secret</h2><p>Choose a secret to inspect its public details.</p></div>}</section>
    <aside className={styles.contextPane}><h2>Real context</h2>{selected ? <Context item={selected} data={data} /> : <p>Select a secret to see resolved relationships.</p>}</aside>
    {createOpen ? <CreateSecretModal worldId={worldId} onClose={() => setCreateOpen(false)} onCreated={resource.retry} /> : null}
  </main>;
}

function Fact({ label, value }: { label: string; value?: string }) { return <div className={styles.fact}><span>{label}</span><strong>{value || 'Not set'}</strong></div>; }
function Context({ item, data }: { item: SecretItem; data: Data }) {
  const resolveAgents = (ids: string[] | undefined) => (ids ?? []).map((id) => data.agents.find((agent) => agent.id === id)).filter(Boolean) as WorldAgent[];
  const aware = resolveAgents(item.aware_character_ids); const suspicious = resolveAgents(item.suspicious_character_ids);
  const threads = (item.related_foreshadowing_ids ?? []).map((id) => data.foreshadowing.find((entry) => entry.id === id)).filter(Boolean) as ForeshadowingItem[];
  const chapter = data.chapters.find((entry) => entry.id === item.chapter_id); const scene = data.scenes.find((entry) => entry.id === item.scene_id);
  return <div className={styles.context}>{aware.length ? <ChipGroup title="Aware" values={aware.map((agent) => ({ id: agent.id, label: agent.display_name || agent.name }))} /> : null}{suspicious.length ? <ChipGroup title="Suspicious" values={suspicious.map((agent) => ({ id: agent.id, label: agent.display_name || agent.name }))} /> : null}{threads.length ? <ChipGroup title="Foreshadowing" values={threads.map((thread) => ({ id: thread.id, label: thread.content }))} /> : null}{chapter && <Fact label="Chapter" value={chapter.title} />}{scene && <Fact label="Scene" value={scene.title} />}{!aware.length && !suspicious.length && !threads.length && !chapter && !scene ? <p>No resolvable relationships recorded.</p> : null}</div>;
}
function ChipGroup({ title, values }: { title: string; values: { id: string; label: string }[] }) { return <div><h3>{title}</h3><div className={styles.chips}>{values.map((value) => <span key={value.id}>{value.label}</span>)}</div></div>; }
