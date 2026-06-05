import ReactMarkdown from 'react-markdown';
import rehypeHighlight from 'rehype-highlight';
import styles from './Cells.module.css';

interface Props {
  text: string;
}

export default function AssistantCell({ text }: Props) {
  return (
    <div className={`${styles.msg} ${styles.msgAssistant}`}>
      <ReactMarkdown rehypePlugins={[rehypeHighlight]}>{text}</ReactMarkdown>
    </div>
  );
}
