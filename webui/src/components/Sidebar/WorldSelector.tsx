import { useState } from 'react';
import { Pencil } from 'lucide-react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import './WorldSelector.css';

export default function WorldSelector() {
  const { state, dispatch } = useAppState();
  const { worlds } = state;
  const [editWorld, setEditWorld] = useState<{
    id: string;
    name: string;
    description: string;
  } | null>(null);

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

  return (
    <div className="world-selector">
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
      {state.worldId && (
        <button
          className="world-edit-btn"
          onClick={() => openEdit(state.worldId!)}
          aria-label="Edit world"
        >
          <Pencil size={14} aria-hidden="true" strokeWidth={2.3} />
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
