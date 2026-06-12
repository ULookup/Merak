import { api } from '../api/client';
import styles from './AgentAvatar.module.css';

interface AgentAvatarProps {
  name: string;
  src?: string | null;
  size?: 'sm' | 'md' | 'lg';
  className?: string;
}

export default function AgentAvatar({ name, src, size = 'md', className }: AgentAvatarProps) {
  const label = (name || 'A').trim().slice(0, 1).toUpperCase();
  const classes = [styles.avatar, styles[size], className].filter(Boolean).join(' ');

  return (
    <div className={classes} aria-label={`${name || 'Agent'} avatar`}>
      {src ? <img className={styles.image} src={api.imageUrl(src)} alt="" /> : label}
    </div>
  );
}
