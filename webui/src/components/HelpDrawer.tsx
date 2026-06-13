import { BookOpen, CheckCircle2, Plug, Settings, Sparkles, X } from 'lucide-react';
import styles from './HelpDrawer.module.css';

interface HelpDrawerProps {
  open: boolean;
  onClose: () => void;
}

const sections = [
  {
    icon: Plug,
    title: 'Start and connect',
    body: 'Run merak serve, then open the WebUI. The client reads /v1/runtime for model, tool, memory, and worldbuilding status. Live responses stream through the session SSE endpoint.',
    steps: [
      'Confirm the Live SSE chip is connected.',
      'Open Settings if model credentials need updating.',
    ],
  },
  {
    icon: Sparkles,
    title: 'Create a world',
    body: 'Use the world selector to create or rename a world. World data comes from /api/worldbuilding/worlds, and story context loads after a world is selected.',
    steps: [
      'Create a world with a name and short premise.',
      'Pick or create an agent session for that world.',
    ],
  },
  {
    icon: BookOpen,
    title: 'Run and stream',
    body: 'Write in the composer to start a run. Merak creates a session run, streams state changes, tool calls, approvals, generated files, and final text back to the timeline.',
    steps: [
      'Choose a model in the sidebar.',
      'Send a scene, character, outline, or rewrite request.',
      'Approve tool calls when permission mode asks.',
    ],
  },
  {
    icon: CheckCircle2,
    title: 'Inspect the work',
    body: 'The inspector keeps story, files, agents, and run diagnostics close to the writing flow. It uses /api/workspace, /api/worldbuilding, and /v1/runs endpoints where available.',
    steps: [
      'Story: chapters, scenes, secrets, threads, and world time.',
      'Files: generated local files with preview and save.',
      'Agents: cards, prompts, diaries, relations, and images.',
      'Run: timeline, replay, and audit fallback.',
    ],
  },
  {
    icon: Settings,
    title: 'Configure Merak',
    body: 'Settings writes model provider, API key, base URL, and output token limits to /api/config/llm. Desktop builds can restart the local runtime from the same panel.',
    steps: [
      'Save config after edits.',
      'Use Test Connection after restart or credential changes.',
      'Check runtime logs if the desktop runtime fails to become ready.',
    ],
  },
];

export default function HelpDrawer({ open, onClose }: HelpDrawerProps) {
  if (!open) return null;

  return (
    <div className={styles.layer}>
      <button
        className={styles.scrim}
        type="button"
        aria-label="Close guide backdrop"
        onClick={onClose}
      />
      <aside className={styles.drawer} role="dialog" aria-modal="true" aria-label="Merak guide">
        <header className={styles.header}>
          <div>
            <div className={styles.kicker}>Guide</div>
            <h2>Merak Workbench</h2>
          </div>
          <button
            className={styles.closeBtn}
            type="button"
            aria-label="Close guide"
            onClick={onClose}
          >
            <X size={18} aria-hidden="true" strokeWidth={2.2} />
          </button>
        </header>

        <div className={styles.intro}>
          A compact tour of the local runtime, worldbuilding workspace, and authoring flow.
        </div>

        <div className={styles.sections}>
          {sections.map((section) => {
            const Icon = section.icon;
            return (
              <section className={styles.section} key={section.title}>
                <div className={styles.sectionHeader}>
                  <Icon size={17} aria-hidden="true" strokeWidth={2.2} />
                  <h3>{section.title}</h3>
                </div>
                <p>{section.body}</p>
                <ul>
                  {section.steps.map((step) => (
                    <li key={step}>{step}</li>
                  ))}
                </ul>
              </section>
            );
          })}
        </div>
      </aside>
    </div>
  );
}
