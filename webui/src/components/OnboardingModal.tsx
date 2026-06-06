import {
  Bot,
  ChevronLeft,
  ChevronRight,
  Files,
  LayoutDashboard,
  MessageSquareText,
  PanelLeft,
  X,
} from 'lucide-react';
import { useEffect, useState } from 'react';
import type { LucideIcon } from 'lucide-react';
import styles from './OnboardingModal.module.css';

interface OnboardingModalProps {
  onClose: () => void;
}

const steps: Array<{
  title: string;
  kicker: string;
  detail: string;
  Icon: LucideIcon;
}> = [
  {
    title: 'Merak Workbench',
    kicker: 'Workspace',
    detail:
      'This is the main authoring surface. The center streams conversations, tool calls, approvals, and generated Markdown while the side panels keep story context nearby.',
    Icon: LayoutDashboard,
  },
  {
    title: 'World, Sessions, Model',
    kicker: 'Left Sidebar',
    detail:
      'Use the left side to choose a world, switch sessions, create a new world, rename or archive sessions, select a model, and check available tools.',
    Icon: PanelLeft,
  },
  {
    title: 'Prompt Modes',
    kicker: 'Composer',
    detail:
      'The quick mode buttons insert structured prompts for scenes, characters, world rules, outlines, and rewrites. They are editable before you send.',
    Icon: MessageSquareText,
  },
  {
    title: 'Story Inspector',
    kicker: 'Right Panel',
    detail:
      'Story shows the current arc, chapter, scene, active voices, foreshadowing, and secret knowledge boundaries so continuity is easier to hold.',
    Icon: Bot,
  },
  {
    title: 'Files And Backend Preview',
    kicker: 'Output Files',
    detail:
      'Files can be searched, previewed, edited, and saved through the WebUI. Missing backend endpoints use local preview data until the server implements the documented API.',
    Icon: Files,
  },
];

export default function OnboardingModal({ onClose }: OnboardingModalProps) {
  const [index, setIndex] = useState(0);
  const step = steps[index];
  const isFirst = index === 0;
  const isLast = index === steps.length - 1;
  const { Icon } = step;

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') onClose();
    }

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [onClose]);

  return (
    <div className={styles.scrim} role="presentation">
      <section className={styles.modal} role="dialog" aria-modal="true" aria-labelledby="guide-title">
        <button className={styles.closeBtn} type="button" onClick={onClose} aria-label="Close guide">
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>

        <div className={styles.iconWrap}>
          <Icon size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>{step.kicker}</div>
        <h2 id="guide-title">{step.title}</h2>
        <p>{step.detail}</p>

        <div className={styles.dots} aria-label="Guide progress">
          {steps.map((item, dotIndex) => (
            <button
              key={item.title}
              type="button"
              className={dotIndex === index ? styles.dotActive : styles.dot}
              onClick={() => setIndex(dotIndex)}
              aria-label={`Show ${item.title}`}
              aria-current={dotIndex === index ? 'step' : undefined}
            />
          ))}
        </div>

        <div className={styles.actions}>
          <button
            className={styles.secondary}
            type="button"
            onClick={() => setIndex((current) => Math.max(0, current - 1))}
            disabled={isFirst}
          >
            <ChevronLeft size={15} aria-hidden="true" strokeWidth={2.4} />
            Back
          </button>
          <button
            className={styles.primary}
            type="button"
            onClick={isLast ? onClose : () => setIndex((current) => Math.min(steps.length - 1, current + 1))}
          >
            {isLast ? 'Start writing' : 'Next'}
            {!isLast && <ChevronRight size={15} aria-hidden="true" strokeWidth={2.4} />}
          </button>
        </div>
      </section>
    </div>
  );
}
