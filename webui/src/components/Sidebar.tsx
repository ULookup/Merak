import WorldSelector from './Sidebar/WorldSelector';
import SessionList from './Sidebar/SessionList';
import ModelSelector from './Sidebar/ModelSelector';
import ToolPanel from './Sidebar/ToolPanel';
import ContextMeter from './Sidebar/ContextMeter';

export default function Sidebar() {
  return (
    <aside className="sidebar">
      <WorldSelector />
      <SessionList />
      <ModelSelector />
      <ToolPanel />
      <ContextMeter />
    </aside>
  );
}
