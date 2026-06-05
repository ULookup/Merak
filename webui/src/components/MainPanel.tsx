import ChatTimeline from './ChatTimeline';
import Composer from './Composer';
import styles from './MainPanel.module.css';

export default function MainPanel() {
  return (
    <main className={styles.main}>
      <ChatTimeline />
      <Composer />
    </main>
  );
}
