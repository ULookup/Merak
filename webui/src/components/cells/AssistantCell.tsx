import ReactMarkdown from 'react-markdown';
import rehypeHighlight from 'rehype-highlight';

interface Props {
  text: string;
}

export default function AssistantCell({ text }: Props) {
  return (
    <div className="msg msg-assistant">
      <ReactMarkdown rehypePlugins={[rehypeHighlight]}>{text}</ReactMarkdown>
    </div>
  );
}
