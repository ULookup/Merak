interface Props { text: string; }

export default function UserCell({ text }: Props) {
  return <div className="msg msg-user">{text}</div>;
}
