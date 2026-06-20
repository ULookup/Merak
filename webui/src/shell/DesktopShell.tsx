import type { ReactNode } from 'react';
import { ChevronDown, Globe2, Search } from 'lucide-react';
import type { AppPage } from '../AppState';
import { useAppState } from '../AppState';
import merakLogo from '../assets/merak-logo.svg';
import { useSafeNavigation } from '../hooks/useSafePageNavigation';
import { useI18n } from '../i18n';
import styles from './DesktopShell.module.css';
import { desktopPages } from './navigation';

interface DesktopShellProps {
  page: AppPage;
  onNavigate: (page: AppPage) => void;
  children: ReactNode;
  overlays?: ReactNode;
}

export default function DesktopShell({ page, onNavigate, children, overlays }: DesktopShellProps) {
  const { state } = useAppState();
  const { requestWorldChange } = useSafeNavigation();
  const { t } = useI18n();
  const selectedWorld = state.worlds.find((world) => world.id === state.worldId);
  const worldName = selectedWorld?.name || t('shell.noWorld');

  return (
    <div className={styles.shell}>
      <aside className={styles.sidebar}>
        <div className={styles.brand}>
          <img src={merakLogo} alt="Merak" />
        </div>

        <div className={styles.worldCard}>
          <Globe2 size={18} aria-hidden="true" />
          <span>{worldName}</span>
          <ChevronDown size={14} aria-hidden="true" />
        </div>

        <div className={styles.search} aria-hidden="true">
          <Search size={15} />
          <span>{t('shell.search')}</span>
          <kbd>Ctrl K</kbd>
        </div>

        <nav className={styles.navigation} aria-label={t('shell.primaryNavigation')}>
          {desktopPages.map(([pageId, label, Icon]) => (
            <button
              key={pageId}
              type="button"
              className={styles.navButton}
              aria-current={page === pageId ? 'page' : undefined}
              onClick={() => onNavigate(pageId)}
            >
              <Icon size={16} aria-hidden="true" strokeWidth={2} />
              <span>{label}</span>
            </button>
          ))}
        </nav>
      </aside>

      <div className={styles.stage}>
        <header className={styles.topbar}>
          <label className={styles.worldSelector}>
            <Globe2 size={16} aria-hidden="true" />
            <span className={styles.srOnly}>{t('shell.worldSelector')}</span>
            <select
              aria-label={t('shell.worldSelector')}
              value={state.worldId ?? ''}
              onChange={(event) => requestWorldChange(event.target.value || null)}
            >
              <option value="">{t('shell.noWorld')}</option>
              {state.worlds.map((world) => (
                <option key={world.id} value={world.id}>
                  {world.name || world.id}
                </option>
              ))}
            </select>
            <ChevronDown size={14} aria-hidden="true" />
          </label>
        </header>
        <main className={styles.content}>
          <div className={styles.pageContent}>{children}</div>
        </main>
      </div>
      {overlays}
    </div>
  );
}
