import { useState } from 'react';
import { useAppState } from '../../AppState';
import { api } from '../../api/client';

export default function WorldSelector() {
  const { state, dispatch } = useAppState();
  const { worlds } = state;
  const [editWorld, setEditWorld] = useState<{ id: string; name: string; description: string } | null>(null);

  function openEdit(worldId: string) {
    const w = worlds.find((w) => w.id === worldId);
    if (w) setEditWorld({ id: w.id, name: w.name || '', description: w.description || '' });
  }

  async function saveEdit() {
    if (!editWorld) return;
    await api.updateWorld(editWorld.id, editWorld.name, editWorld.description);
    dispatch({
      type: 'SET_WORLDS',
      worlds: worlds.map((w) =>
        w.id === editWorld.id ? { ...w, name: editWorld.name, description: editWorld.description } : w
      ),
    });
    setEditWorld(null);
  }

  return (
    <div className="world-selector">
      <select
        value={state.worldId ?? ''}
        onChange={(e) => dispatch({ type: 'SET_WORLD', worldId: e.target.value || null })}
      >
        <option value="">None</option>
        {worlds.map((world) => (
          <option key={world.id} value={world.id}>
            {world.name || world.id}
          </option>
        ))}
      </select>
      {state.worldId && (
        <button className="world-edit-btn" onClick={() => openEdit(state.worldId!)} title="Edit world">
          ✎
        </button>
      )}

      {editWorld && (
        <div className="modal-overlay" onClick={() => setEditWorld(null)}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
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
            <div className="modal-actions">
              <button onClick={saveEdit}>Save</button>
              <button onClick={() => setEditWorld(null)}>Cancel</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
