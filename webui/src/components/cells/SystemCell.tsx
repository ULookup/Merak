interface Props {
  text: string;
  error?: boolean;
}

export default function SystemCell({ text, error }: Props) {
  return <div className={`msg-system ${error ? 'msg-system-error' : ''}`}>{text}</div>;
}
