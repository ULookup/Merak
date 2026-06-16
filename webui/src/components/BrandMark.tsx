import logoUrl from '../assets/merak-logo.svg';
import styles from './BrandMark.module.css';

interface BrandMarkProps {
  compact?: boolean;
}

export default function BrandMark({ compact = false }: BrandMarkProps) {
  return (
    <div className={`${styles.brand} ${compact ? styles.compact : ''}`}>
      <img className={styles.logo} src={logoUrl} alt="Merak wordmark logo" />
      {!compact && (
        <div>
          <div className={styles.wordmark}>MERAK</div>
          <div className={styles.tagline}>type your world</div>
        </div>
      )}
    </div>
  );
}
