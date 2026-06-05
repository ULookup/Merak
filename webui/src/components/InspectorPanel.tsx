import { useAppState, type InspectorTab } from '../AppState';
import styles from './InspectorPanel.module.css';

interface InspectorPanelProps {
  open?: boolean;
  onClose?: () => void;
}

const tabs: Array<{ id: InspectorTab; label: string }> = [
  { id: 'story', label: 'Story' },
  { id: 'files', label: 'Files' },
  { id: 'agents', label: 'Agents' },
  { id: 'run', label: 'Run' },
];

function EmptyState() {
  return <div className={styles.empty}>Select a world to load story context.</div>;
}

function statusLabel(value: string | undefined) {
  return value ? value.replace(/_/g, ' ') : 'open';
}

export default function InspectorPanel({ open = true, onClose }: InspectorPanelProps) {
  const { state, dispatch } = useAppState();
  const selectedWorld = state.worlds.find((world) => world.id === state.worldId);
  const used = state.usage.inputTokens + state.usage.outputTokens;
  const model = state.metadata?.models?.find((m) => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const pct = Math.min(100, Math.round((used / budget) * 100));

  return (
    <aside
      className={`${styles.panel} ${open ? styles.panelOpen : ''}`}
      aria-label="Story inspector"
      data-testid="inspector-panel"
    >
      <div className={styles.mobileScrim} onClick={onClose} />
      <div className={styles.surface}>
        <header className={styles.header}>
          <div>
            <div className={styles.kicker}>Inspector</div>
            <h2>{state.inspectorTab === 'files' ? 'Output Files' : 'Story Context'}</h2>
          </div>
          <button className={styles.closeBtn} onClick={onClose} aria-label="Close inspector">
            ×
          </button>
        </header>

        <div className={styles.tabs} role="tablist" aria-label="Inspector tabs">
          {tabs.map((tab) => (
            <button
              key={tab.id}
              className={`${styles.tab} ${state.inspectorTab === tab.id ? styles.tabActive : ''}`}
              onClick={() => dispatch({ type: 'SET_INSPECTOR_TAB', tab: tab.id })}
              role="tab"
              aria-selected={state.inspectorTab === tab.id}
            >
              {tab.label}
            </button>
          ))}
        </div>

        {!state.worldId && state.inspectorTab !== 'files' ? (
          <EmptyState />
        ) : (
          <div className={styles.content}>
            {state.inspectorTab === 'files' && (
              <>
                <section className={styles.outputCard}>
                  <div>
                    <div className={styles.sectionTitle}>Output Directory</div>
                    {state.outputDirectory ? (
                      <code className={styles.pathLine}>{state.outputDirectory}</code>
                    ) : (
                      <p className={styles.muted}>
                        Choose a directory in the run prompt. Generated articles stay on disk.
                      </p>
                    )}
                  </div>
                  <button
                    className={styles.entryButton}
                    type="button"
                    disabled={!state.outputDirectory}
                    aria-label="Open output folder"
                  >
                    Open folder
                  </button>
                </section>

                <section className={styles.section}>
                  <div className={styles.sectionTitle}>Generated Files</div>
                  {state.generatedFiles.length === 0 ? (
                    <p className={styles.muted}>
                      Files written by agents will appear here with quick entry points.
                    </p>
                  ) : (
                    <div className={styles.fileList}>
                      {state.generatedFiles.map((file) => (
                        <article className={styles.fileItem} key={file.id}>
                          <div>
                            <strong>{file.title}</strong>
                            <code>{file.path}</code>
                          </div>
                          <button
                            className={styles.entryButton}
                            type="button"
                            aria-label={`Open ${file.title} in editor`}
                          >
                            Open in editor
                          </button>
                        </article>
                      ))}
                    </div>
                  )}
                </section>
              </>
            )}

            {state.inspectorTab === 'story' && (
              <>
                <section className={styles.heroBlock}>
                  <div className={styles.worldName}>{selectedWorld?.name ?? state.worldId}</div>
                  <p>{selectedWorld?.description || 'No world description yet.'}</p>
                  <div className={styles.timeRow}>
                    <span>World time</span>
                    <strong>{state.worldTime ?? 'Not set'}</strong>
                  </div>
                </section>
                <section className={styles.section}>
                  <div className={styles.sectionTitle}>Active Voices</div>
                  {state.agents.length === 0 ? (
                    <p className={styles.muted}>No character voices loaded.</p>
                  ) : (
                    <div className={styles.voiceStrip}>
                      {state.agents.slice(0, 4).map((agent) => (
                        <span key={agent.id}>{agent.display_name || agent.name}</span>
                      ))}
                    </div>
                  )}
                </section>
                <section className={styles.section}>
                  <div className={styles.sectionTitle}>Open Foreshadowing</div>
                  {state.foreshadowing.length === 0 ? (
                    <p className={styles.muted}>No open threads loaded.</p>
                  ) : (
                    state.foreshadowing.slice(0, 5).map((item) => (
                      <div className={styles.thread} key={item.id}>
                        <span>{statusLabel(item.status)}</span>
                        {item.content}
                      </div>
                    ))
                  )}
                </section>
                <section className={styles.section}>
                  <div className={styles.sectionTitle}>Secrets</div>
                  {state.secrets.length === 0 ? (
                    <p className={styles.muted}>No secret boundaries loaded.</p>
                  ) : (
                    state.secrets.slice(0, 4).map((item) => (
                      <div className={styles.secret} key={item.id}>
                        {item.title ?? item.content ?? item.id}
                      </div>
                    ))
                  )}
                </section>
              </>
            )}

            {state.inspectorTab === 'agents' && (
              <section className={styles.section}>
                <div className={styles.sectionTitle}>Character Voices</div>
                {state.agents.length === 0 ? (
                  <p className={styles.muted}>No agents loaded for this world.</p>
                ) : (
                  state.agents.map((agent) => (
                    <div className={styles.agent} key={agent.id}>
                      <div className={styles.avatar}>
                        {(agent.display_name || agent.name).slice(0, 1)}
                      </div>
                      <div>
                        <strong>{agent.display_name || agent.name}</strong>
                        <span>{agent.kind}</span>
                      </div>
                    </div>
                  ))
                )}
              </section>
            )}

            {state.inspectorTab === 'run' && (
              <>
                <section className={styles.runCard}>
                  <span className={styles.pulse} />
                  <div>
                    <div className={styles.sectionTitle}>Current Run</div>
                    <strong>{state.status.replace(/_/g, ' ')}</strong>
                    <p>{state.currentRun ?? 'No active run'}</p>
                  </div>
                </section>
                <section className={styles.section}>
                  <div className={styles.sectionTitle}>Context Health</div>
                  <div className={styles.meter}>
                    <div style={{ width: `${pct}%` }} />
                  </div>
                  <p className={styles.muted}>
                    {(used / 1000).toFixed(1)}K / {(budget / 1000).toFixed(0)}K tokens
                  </p>
                </section>
                {state.worldbuildingStatus === 'error' && (
                  <div className={styles.error}>{state.worldbuildingError}</div>
                )}
              </>
            )}
          </div>
        )}
      </div>
    </aside>
  );
}
