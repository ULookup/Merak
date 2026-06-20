import { useEffect, useMemo, useRef, useState } from 'react';
import { Eye, Filter, KeyRound, Pencil, Plus, RefreshCw, Trash2 } from 'lucide-react';
import { api } from '../api/client';
import type { SecretItem, WorldAgent } from '../api/types';
import CreateSecretModal from '../components/Inspector/CreateSecretModal';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import styles from './SecretsPage.module.css';

type Data = { items: SecretItem[]; agents: WorldAgent[]; contextError: string | null };

export default function SecretsPage({ worldId }: { worldId: string }) {
  const resource = useResource(`secrets:${worldId}`, async () => {
    const [secrets, agents] = await Promise.allSettled([api.listSecrets(worldId), api.listAgents(worldId)]);
    if (secrets.status === 'rejected') throw secrets.reason;
    const failures = [agents].filter((result) => result.status === 'rejected') as PromiseRejectedResult[];
    return {
      items: secrets.value.secrets ?? secrets.value.items ?? [],
      agents: agents.status === 'fulfilled' ? agents.value.agents : [],
      contextError: failures.length ? failures.map((result) => result.reason instanceof Error ? result.reason.message : String(result.reason)).join(' ') : null,
    };
  });
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [filter, setFilter] = useState('all');
  const [revealedTruth, setRevealedTruth] = useState<{ secretId: string; truth: string } | null>(null);
  const [createOpen, setCreateOpen] = useState(false);
  const [editing, setEditing] = useState(false);
  const [publicVersion, setPublicVersion] = useState('');
  const [removed, setRemoved] = useState<string[]>([]);
  const [mutationError, setMutationError] = useState<string | null>(null);
  const [pendingMutation, setPendingMutation] = useState<string | null>(null);
  const pendingMutationRef = useRef(false);
  const generationRef = useRef(0);
  const committedWorldRef = useRef(worldId);
  const mountedRef = useRef(false);
  const data: Data | null = resource.data;
  const items = useMemo(() => (data?.items ?? []).filter((item) => !removed.includes(item.id)), [data, removed]);
  const filtered = filter === 'all' ? items : items.filter((item) => item.status === filter);
  const selected = filtered.find((item) => item.id === selectedId) ?? null;

  useEffect(() => { setSelectedId(null); setRemoved([]); setRevealedTruth(null); setEditing(false); setCreateOpen(false); setPendingMutation(null); pendingMutationRef.current = false; setMutationError(null); }, [worldId]);
  useEffect(() => {
    mountedRef.current = true;
    if (committedWorldRef.current !== worldId) {
      committedWorldRef.current = worldId;
      generationRef.current += 1;
    }
    return () => { mountedRef.current = false; generationRef.current += 1; };
  }, [worldId]);
  useEffect(() => { setRevealedTruth(null); setEditing(false); setPublicVersion(selected?.public_version ?? ''); }, [selected?.id]);
  useEffect(() => { if (selectedId && !filtered.some((item) => item.id === selectedId)) setSelectedId(filtered[0]?.id ?? null); }, [filtered, selectedId]);

  async function save() {
    if (!selected || pendingMutationRef.current || !window.confirm('Update this secret?')) return;
    const operationWorld = worldId;
    const operationGeneration = generationRef.current;
    pendingMutationRef.current = true;
    setPendingMutation('update'); setMutationError(null);
    try { await api.patchSecret(operationWorld, selected.id, { public_version: publicVersion }); if (mountedRef.current && generationRef.current === operationGeneration) { setEditing(false); resource.retry(); } }
    catch (error) { if (mountedRef.current && generationRef.current === operationGeneration) setMutationError((error as Error).message); }
    finally { if (mountedRef.current && generationRef.current === operationGeneration) { pendingMutationRef.current = false; setPendingMutation(null); } }
  }
  async function remove() {
    if (!selected || pendingMutationRef.current || !window.confirm('Delete this secret?')) return;
    const operationWorld = worldId; const id = selected.id;
    const operationGeneration = generationRef.current;
    pendingMutationRef.current = true;
    setPendingMutation('delete'); setMutationError(null);
    try { await api.deleteSecret(operationWorld, id); if (mountedRef.current && generationRef.current === operationGeneration) { setRemoved((ids) => [...ids, id]); setSelectedId(filtered.find((item) => item.id !== id)?.id ?? null); setRevealedTruth(null); resource.retry(); } }
    catch (error) { if (mountedRef.current && generationRef.current === operationGeneration) setMutationError((error as Error).message); }
    finally { if (mountedRef.current && generationRef.current === operationGeneration) { pendingMutationRef.current = false; setPendingMutation(null); } }
  }
  function changeFilter(nextFilter: string) {
    setFilter(nextFilter);
    const visible = nextFilter === 'all' ? items : items.filter((item) => item.status === nextFilter);
    setSelectedId(visible[0]?.id ?? null);
    setRevealedTruth(null);
  }

  if (!data) return <PageState loading={resource.status === 'loading'} loadingLabel="Loading secrets" error={resource.error} onRetry={resource.retry} />;
  return <main className={styles.workspace}>
    <aside className={styles.listPane}><header><div><span>Knowledge boundaries</span><h1>Secrets</h1></div><div className={styles.tools}><button aria-label="Refresh secrets" onClick={resource.retry}><RefreshCw /></button><button aria-label="Create secret" disabled={Boolean(pendingMutation)} onClick={() => setCreateOpen(true)}><Plus /></button></div></header>
      <label className={styles.filter}><Filter /><span>Status</span><select value={filter} onChange={(event) => changeFilter(event.target.value)}><option value="all">All</option><option value="active">Active</option><option value="exposed">Exposed</option><option value="abandoned">Abandoned</option></select></label>
      {(resource.error || mutationError || data.contextError) && <div role="alert" className={styles.warning}>{mutationError ?? resource.error?.message ?? data.contextError}</div>}
      {filtered.length ? <ResourceList items={filtered} selectedId={selectedId} getId={(item) => item.id} onSelect={setSelectedId} ariaLabel="Secrets" renderItem={(item) => <div className={styles.listItem}><KeyRound /><div><strong>{item.title || item.public_version || 'Untitled secret'}</strong><span>{item.status ?? 'No status'}</span></div></div>} /> : <p className={styles.empty}>No secrets match this filter.</p>}
      <footer>{filtered.length} {filtered.length === 1 ? 'secret' : 'secrets'}</footer></aside>
    <section className={styles.detail} aria-label="Secret detail">{selected ? <><header className={styles.detailHeader}><div><span>Secret detail</span><h2>{selected.title || selected.public_version || 'Untitled secret'}</h2></div><div className={styles.actions}><button aria-label="Edit secret" disabled={Boolean(pendingMutation)} onClick={() => setEditing(true)}><Pencil /> Edit</button><button aria-label="Delete secret" disabled={Boolean(pendingMutation)} onClick={remove}><Trash2 /> Delete</button></div></header>
      <div className={styles.cards}><Fact label="Status" value={selected.status} /><Fact label="Stakes" value={selected.stakes} /></div>
      <section className={styles.block}><h3>Public version</h3>{editing ? <><textarea aria-label="Public version" disabled={Boolean(pendingMutation)} value={publicVersion} onChange={(event) => setPublicVersion(event.target.value)} /><button className={styles.primary} disabled={Boolean(pendingMutation)} onClick={save}>Save changes</button></> : <p>{selected.public_version || 'No public version recorded.'}</p>}</section>
      <section className={styles.truth}><h3>Truth</h3>{revealedTruth?.secretId === selected.id ? <p>{revealedTruth.truth || 'No truth recorded.'}</p> : <div><KeyRound /><p>Hidden until you explicitly reveal it.</p><button aria-label="Reveal truth" onClick={() => setRevealedTruth({ secretId: selected.id, truth: selected.truth ?? selected.content ?? '' })}><Eye /> Reveal truth</button></div>}</section>
    </> : <div className={styles.pick}><KeyRound /><h2>Select a secret</h2><p>Choose a secret to inspect its public details.</p></div>}</section>
    <aside className={styles.contextPane}><h2>Real context</h2>{selected ? <Context item={selected} data={data} /> : <p>Select a secret to see resolved relationships.</p>}</aside>
    {createOpen ? <CreateSecretModal worldId={worldId} onClose={() => setCreateOpen(false)} onCreated={resource.retry} /> : null}
  </main>;
}

function Fact({ label, value }: { label: string; value?: string }) { return <div className={styles.fact}><span>{label}</span><strong>{value || 'Not set'}</strong></div>; }
function Context({ item, data }: { item: SecretItem; data: Data }) {
  const resolveAgents = (ids: string[] | undefined) => (ids ?? []).map((id) => data.agents.find((agent) => agent.id === id)).filter(Boolean) as WorldAgent[];
  const aware = resolveAgents(item.aware_character_ids); const suspicious = resolveAgents(item.suspicious_character_ids);
  return <div className={styles.context}>{aware.length ? <ChipGroup title="Aware" values={aware.map((agent) => ({ id: agent.id, label: agent.display_name || agent.name }))} /> : null}{suspicious.length ? <ChipGroup title="Suspicious" values={suspicious.map((agent) => ({ id: agent.id, label: agent.display_name || agent.name }))} /> : null}{!aware.length && !suspicious.length ? <p>No resolvable character relationships recorded.</p> : null}</div>;
}
function ChipGroup({ title, values }: { title: string; values: { id: string; label: string }[] }) { return <div><h3>{title}</h3><div className={styles.chips}>{values.map((value) => <span key={value.id}>{value.label}</span>)}</div></div>; }
