import ContextMeter from './Sidebar/ContextMeter';
import ModelSelector from './Sidebar/ModelSelector';
import SessionList from './Sidebar/SessionList';
import ToolPanel from './Sidebar/ToolPanel';
import WorldSelector from './Sidebar/WorldSelector';

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
