import styles from './Cells.module.css';

interface Props {
  text: string;
}

export default function UserCell({ text }: Props) {
  return <div className={`${styles.msg} ${styles.msgUser}`}>{text}</div>;
}
