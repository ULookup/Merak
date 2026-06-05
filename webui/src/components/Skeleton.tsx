import styles from './Skeleton.module.css';

export default function Skeleton() {
  return (
    <div className={styles.layout}>
      {/* Sidebar skeleton */}
      <aside className={styles.sidebar}>
        <div className={styles.sbSection}>
          <div className={`${styles.shimmer} ${styles.labelPlaceholder}`} />
          <div className={`${styles.shimmer} ${styles.selectPlaceholder}`} />
        </div>
        <div className={styles.sbSection}>
          <div className={`${styles.shimmer} ${styles.labelPlaceholder}`} />
          <div className={`${styles.shimmer} ${styles.selectPlaceholder}`} />
        </div>
        <div className={`${styles.sbSection} ${styles.sbGrow}`}>
          <div className={`${styles.shimmer} ${styles.labelPlaceholder}`} />
          <div className={`${styles.shimmer} ${styles.itemPlaceholder}`} />
          <div className={`${styles.shimmer} ${styles.itemPlaceholder}`} />
          <div className={`${styles.shimmer} ${styles.itemPlaceholder}`} />
        </div>
      </aside>

      {/* Main panel skeleton */}
      <main className={styles.main}>
        <div className={styles.chatArea}>
          <div className={`${styles.shimmer} ${styles.msgPlaceholder} ${styles.msgRight}`} />
          <div className={`${styles.shimmer} ${styles.msgPlaceholder} ${styles.msgLeft}`} />
          <div className={`${styles.shimmer} ${styles.msgPlaceholder} ${styles.msgRight}`} />
        </div>
        <div className={styles.composerArea}>
          <div className={`${styles.shimmer} ${styles.inputPlaceholder}`} />
        </div>
      </main>
    </div>
  );
}
