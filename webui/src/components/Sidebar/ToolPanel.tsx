import { Plug, Wrench } from 'lucide-react';
import { useAppState } from '../../AppState';
import { useI18n } from '../../i18n';
import styles from '../Sidebar.module.css';
import tpStyles from './ToolPanel.module.css';

export default function ToolPanel() {
  const { t } = useI18n();
  const { state } = useAppState();
  const tools = state.metadata?.tools ?? [];

  return (
    <div className={styles.section}>
      <div className={styles.label}>{t('sidebar.tools').replace('{count}', String(tools.length))}</div>
      <div className={tpStyles.panel}>
        {tools.length === 0 ? (
          <div className={tpStyles.empty}>{t('sidebar.noTools')}</div>
        ) : (
          tools.map((tool) => (
            <div key={tool.name} className={tpStyles.row} data-testid="tool-row">
              <div className={tpStyles.rowMain}>
                <div className={tpStyles.rowName}>
                  {tool.source === 'mcp' ? (
                    <Plug className={tpStyles.icon} size={14} aria-hidden="true" strokeWidth={2.3} />
                  ) : (
                    <Wrench className={tpStyles.icon} size={14} aria-hidden="true" strokeWidth={2.3} />
                  )}
                  <span>{tool.name}</span>
                </div>
                {tool.description && <p>{tool.description}</p>}
              </div>
              <span
                className={`${tpStyles.badge} ${tool.source === 'mcp' ? tpStyles.badgeAsk : tpStyles.badgeSafe}`}
              >
                {tool.source === 'mcp' ? t('sidebar.toolMcp') : t('sidebar.toolBuiltin')}
              </span>
            </div>
          ))
        )}
      </div>
    </div>
  );
}
