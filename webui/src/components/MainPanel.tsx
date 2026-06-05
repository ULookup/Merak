import ChatTimeline from './ChatTimeline';
import Composer from './Composer';

export default function MainPanel() {
  return (
    <main className="main">
      <ChatTimeline />
      <Composer />
    </main>
  );
}
