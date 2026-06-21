import { useState } from 'react';
import { CheckCircle2, Clock3, PanelsTopLeft, RefreshCw, Users } from 'lucide-react';
import { api } from '../api/client';
import type { EndSceneResponse, StoryScene } from '../api/types';
import EndSceneModal from '../components/Inspector/EndSceneModal';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import ResponsivePane from '../components/layout/ResponsivePane';
import { useResource } from '../hooks/useResource';
import styles from './ScenesPage.module.css';

export default function ScenesPage({ worldId }: { worldId: string }) {
  const resource = useResource(`scenes:${worldId}`, () => api.listScenes(worldId));
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [completed, setCompleted] = useState<Record<string, EndSceneResponse>>({});
  const [endTarget, setEndTarget] = useState<StoryScene | null>(null);
  const scenes = resource.data?.scenes ?? [];
  const selectedBase = scenes.find((scene) => scene.id === selectedId) ?? null;
  const selected =
    selectedBase && completed[selectedBase.id]
      ? { ...selectedBase, status: 'completed' }
      : selectedBase;

  if (!resource.data) {
    return (
      <PageState
        loading={resource.status === 'loading'}
        loadingLabel="Loading scenes"
        error={resource.error}
        onRetry={resource.retry}
      />
    );
  }
  if (!scenes.length) {
    return (
      <main className={styles.page}>
        <PageState
          isEmpty
          emptyTitle="No scenes yet"
          emptyDescription="Scenes will appear here after they are added to a chapter."
        />
      </main>
    );
  }

  return (
    <main className={styles.workspace}>
      <ResponsivePane
        label="Scenes"
        closeOnSelect
        className={styles.listPane}
        aria-hidden={endTarget ? 'true' : undefined}
        inert={endTarget ? true : undefined}
      >
        <header>
          <div>
            <span>Narrative plan</span>
            <h1>Scenes</h1>
          </div>
          <button type="button" onClick={resource.retry} aria-label="Refresh scenes">
            <RefreshCw aria-hidden="true" />
          </button>
        </header>
        {resource.error ? (
          <div role="alert" className={styles.warning}>
            {resource.error.message}
          </div>
        ) : null}
        <ResourceList
          items={scenes}
          selectedId={selectedId}
          getId={(scene) => scene.id}
          onSelect={setSelectedId}
          ariaLabel="Scenes"
          renderItem={(scene) => (
            <SceneListItem scene={scene} completed={Boolean(completed[scene.id])} />
          )}
        />
        <footer>
          {scenes.length} {scenes.length === 1 ? 'scene' : 'scenes'}
        </footer>
      </ResponsivePane>
      <section
        className={styles.detail}
        aria-label="Scene detail"
        aria-hidden={endTarget ? 'true' : undefined}
        inert={endTarget ? true : undefined}
      >
        {selected ? (
          <>
            <header className={styles.detailHeader}>
              <div>
                <span>Scene detail</span>
                <h2>{selected.title}</h2>
              </div>
              {selected.status !== 'completed' ? (
                <button type="button" onClick={() => setEndTarget(selected)}>
                  End scene
                </button>
              ) : null}
            </header>
            <div className={styles.facts}>
              <Fact
                icon={<CheckCircle2 aria-hidden="true" />}
                label="Status"
                value={selected.status}
              />
              <Fact
                icon={<PanelsTopLeft aria-hidden="true" />}
                label="Chapter ID"
                value={selected.chapter_id}
              />
              <Fact
                icon={<Clock3 aria-hidden="true" />}
                label="World time"
                value={selected.world_time || 'Not set'}
              />
              <Fact
                icon={<Users aria-hidden="true" />}
                label="Participants"
                value={
                  selected.participant_ids.length ? selected.participant_ids.join(', ') : 'None'
                }
              />
            </div>
            <section className={styles.updated}>
              <span>Last updated</span>
              <strong>{selected.updated_at}</strong>
            </section>
          </>
        ) : (
          <div className={styles.pick}>
            <PanelsTopLeft aria-hidden="true" />
            <h2>Select a scene</h2>
            <p>Choose a scene to inspect its current API-backed details.</p>
          </div>
        )}
      </section>
      {endTarget ? (
        <EndSceneModal
          worldId={worldId}
          sceneId={endTarget.id}
          sceneTitle={endTarget.title}
          onClose={() => setEndTarget(null)}
          onEnded={(result) =>
            setCompleted((previous) => ({ ...previous, [endTarget.id]: result }))
          }
        />
      ) : null}
    </main>
  );
}

function SceneListItem({ scene, completed }: { scene: StoryScene; completed: boolean }) {
  return (
    <div className={styles.listItem}>
      <span className={styles.dot} />
      <div>
        <strong>{scene.title}</strong>
        <span>
          {completed ? 'completed' : scene.status} · {scene.world_time || 'No world time'}
        </span>
      </div>
    </div>
  );
}

function Fact({ icon, label, value }: { icon: React.ReactNode; label: string; value: string }) {
  return (
    <div className={styles.fact}>
      {icon}
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}
