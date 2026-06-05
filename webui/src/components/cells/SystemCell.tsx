import styles from './Cells.module.css';

interface Props {
  text: string;
  error?: boolean;
}

export default function SystemCell({ text, error }: Props) {
  return <div className={`${styles.system} ${error ? styles.systemError : ''}`}>{text}</div>;
}
