import { useState } from 'react';
import { Pencil, Plus, Trash2 } from 'lucide-react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import { useToast } from '../Toast';
import styles from './WorldSelector.module.css';

export default function WorldSelector() {
  const { state, dispatch } = useAppState();
  const { showToast } = useToast();
  const { worlds } = state;
  const [editWorld, setEditWorld] = useState<{
    id: string;
    name: string;
    description: string;
  } | null>(null);
  const [createOpen, setCreateOpen] = useState(false);
  const [newWorld, setNewWorld] = useState({ name: '', description: '' });

  function openEdit(worldId: string) {
    const world = worlds.find((item) => item.id === worldId);
    if (world) {
      setEditWorld({
        id: world.id,
        name: world.name || '',
        description: world.description || '',
      });
    }
  }

  async function saveEdit() {
    if (!editWorld) return;
    await api.updateWorld(editWorld.id, editWorld.name, editWorld.description);
    dispatch({
      type: 'SET_WORLDS',
      worlds: worlds.map((world) =>
        world.id === editWorld.id
          ? { ...world, name: editWorld.name, description: editWorld.description }
          : world,
      ),
    });
    setEditWorld(null);
  }

  async function deleteWorld() {
    if (!editWorld) return;
    const ok = window.confirm(`Delete world "${editWorld.name || editWorld.id}"?`);
    if (!ok) return;
    try {
      await api.deleteWorld(editWorld.id);
      const nextWorlds = worlds.filter((world) => world.id !== editWorld.id);
      dispatch({ type: 'SET_WORLDS', worlds: nextWorlds });
      if (state.worldId === editWorld.id) {
        dispatch({ type: 'SET_WORLD', worldId: null });
      }
      setEditWorld(null);
      showToast('World deleted.', 'success');
    } catch (error) {
      showToast(error instanceof Error ? error.message : 'Could not delete world.', 'error');
    }
  }

  async function createWorld() {
    const name = newWorld.name.trim();
    if (!name) return;
    try {
      const res = await api.createWorld(name, newWorld.description.trim());
      const world = {
        id: res.world_id,
        name: res.name,
        description: res.description || '',
        created_at: new Date().toISOString(),
      };
      dispatch({ type: 'SET_WORLDS', worlds: [world, ...worlds] });
      dispatch({ type: 'SET_WORLD', worldId: world.id });
      showToast('World created in the workbench preview.', 'success');
    } catch (error) {
      showToast(error instanceof Error ? error.message : 'Could not create world.', 'error');
    } finally {
      setCreateOpen(false);
      setNewWorld({ name: '', description: '' });
    }
  }

  return (
    <div className={styles.selector}>
      <select
        value={state.worldId ?? ''}
        onChange={(e) => dispatch({ type: 'SET_WORLD', worldId: e.target.value || null })}
        aria-label="Select world"
      >
        <option value="">None</option>
        {worlds.map((world) => (
          <option key={world.id} value={world.id}>
            {world.name || world.id}
          </option>
        ))}
      </select>
      <button
        className={styles.editBtn}
        onClick={() => setCreateOpen(true)}
        aria-label="Create world"
        title="Create world"
      >
        <Plus size={14} aria-hidden="true" strokeWidth={2.3} />
      </button>
      {state.worldId && (
        <button
          className={styles.editBtn}
          onClick={() => openEdit(state.worldId!)}
          aria-label="Edit world"
        >
          <Pencil size={14} aria-hidden="true" strokeWidth={2.3} />
        </button>
      )}

      {createOpen && (
        <div className={styles.overlay} onClick={() => setCreateOpen(false)}>
          <div className={styles.modal} onClick={(e) => e.stopPropagation()}>
            <h3>Create World</h3>
            <label>
              Name
              <input
                value={newWorld.name}
                onChange={(e) => setNewWorld({ ...newWorld, name: e.target.value })}
              />
            </label>
            <label>
              Description
              <textarea
                value={newWorld.description}
                onChange={(e) => setNewWorld({ ...newWorld, description: e.target.value })}
                rows={3}
              />
            </label>
            <div className={styles.actions}>
              <button onClick={createWorld}>Create</button>
              <button onClick={() => setCreateOpen(false)}>Cancel</button>
            </div>
          </div>
        </div>
      )}

      {editWorld && (
        <div className={styles.overlay} onClick={() => setEditWorld(null)}>
          <div className={styles.modal} onClick={(e) => e.stopPropagation()}>
            <h3>Edit World</h3>
            <label>
              Name
              <input
                value={editWorld.name}
                onChange={(e) => setEditWorld({ ...editWorld, name: e.target.value })}
              />
            </label>
            <label>
              Description
              <textarea
                value={editWorld.description}
                onChange={(e) => setEditWorld({ ...editWorld, description: e.target.value })}
                rows={3}
              />
            </label>
            <div className={styles.actions}>
              <button onClick={saveEdit}>Save</button>
              <button onClick={deleteWorld} className={styles.danger}>
                <Trash2 size={14} aria-hidden="true" />
                Delete
              </button>
              <button onClick={() => setEditWorld(null)}>Cancel</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
