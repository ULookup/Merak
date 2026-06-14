import { BookOpen, FileText, MessageSquareText, Sparkles, UsersRound, X } from 'lucide-react';
import { useI18n } from '../i18n';
import styles from './HelpDrawer.module.css';

interface HelpDrawerProps {
  open: boolean;
  onClose: () => void;
}

const zhSteps = [
  {
    title: '和 GodAgent 说你想写什么',
    text: '可以直接描述题材、主角、情绪、冲突，GodAgent 会帮你整理成可继续创作的方向。',
    Icon: MessageSquareText,
  },
  {
    title: '创建人物、场景和世界规则',
    text: '右侧“创建”里可以补人物、场景、秘密和伏笔。不会写也没关系，先填你知道的部分。',
    Icon: UsersRound,
  },
  {
    title: '检查故事资料',
    text: '“故事”会汇总当前世界、正在写的场景、角色记忆、秘密和伏笔，方便你保持前后一致。',
    Icon: BookOpen,
  },
  {
    title: '整理成小说文本',
    text: '让 GodAgent 写下一场、润色片段或汇总章节。生成的文稿会出现在“文稿”里，可以继续编辑。',
    Icon: FileText,
  },
];

const enSteps = [
  {
    title: 'Tell GodAgent what you want to write',
    text: 'Describe the genre, main character, mood, conflict, or ending. GodAgent turns that into a workable direction.',
    Icon: MessageSquareText,
  },
  {
    title: 'Create people, scenes, and rules',
    text: 'Use Create to add characters, scenes, secrets, and foreshadowing. Fill only what you know now.',
    Icon: UsersRound,
  },
  {
    title: 'Review story context',
    text: 'Story gathers the current world, active scene, character memories, secrets, and clues so the draft stays consistent.',
    Icon: BookOpen,
  },
  {
    title: 'Shape it into novel text',
    text: 'Ask GodAgent for the next scene, a polish pass, or a chapter draft. Drafts appear in Drafts and remain editable.',
    Icon: FileText,
  },
];

export default function HelpDrawer({ open, onClose }: HelpDrawerProps) {
  const { locale, t } = useI18n();
  const steps = locale === 'zh' ? zhSteps : enSteps;

  if (!open) return null;

  return (
    <div className={styles.layer}>
      <div className={styles.scrim} onClick={onClose} />
      <aside className={styles.drawer} role="dialog" aria-modal="true" aria-label={t('help.title')}>
        <header className={styles.header}>
          <div>
            <div className={styles.kicker}>Merak</div>
            <h2>{t('help.title')}</h2>
          </div>
          <button type="button" className={styles.closeBtn} onClick={onClose} aria-label={t('help.close')}>
            <X size={17} aria-hidden="true" strokeWidth={2.4} />
          </button>
        </header>

        <div className={styles.hero}>
          <Sparkles size={18} aria-hidden="true" strokeWidth={2.2} />
          <p>
            {locale === 'zh'
              ? 'Merak 像一间安静的创作室：你负责想象，GodAgent 帮你把想法整理成人物、设定、场景和文稿。'
              : 'Merak works like a quiet writing room: you imagine, and GodAgent helps organize people, setting, scenes, and drafts.'}
          </p>
        </div>

        <ol className={styles.steps}>
          {steps.map(({ title, text, Icon }) => (
            <li key={title} className={styles.step}>
              <div className={styles.stepIcon}>
                <Icon size={16} aria-hidden="true" strokeWidth={2.2} />
              </div>
              <div>
                <h3>{title}</h3>
                <p>{text}</p>
              </div>
            </li>
          ))}
        </ol>

        <section className={styles.tip}>
          <h3>{locale === 'zh' ? '一句话开始' : 'Start with one sentence'}</h3>
          <p>
            {locale === 'zh'
              ? '例如：“帮我写一个雪夜边境城市里的悬疑故事，主角是会说谎的巡夜人。”'
              : 'For example: “Help me write a mystery in a snowy border city. The main character is a night watchman who lies.”'}
          </p>
        </section>
      </aside>
    </div>
  );
}
