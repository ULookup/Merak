import { useAppState } from '../../AppState';
import { useI18n } from '../../i18n';
import styles from '../Sidebar.module.css';

function formatProvider(provider: string | undefined) {
  if (!provider) return 'Local';
  if (provider.toLowerCase() === 'openai') return 'OpenAI';
  if (provider.toLowerCase() === 'anthropic') return 'Anthropic';
  return provider;
}

function formatTokenWindow(tokens: number | undefined) {
  if (!tokens) return 'unknown';
  return tokens >= 1000 ? `${Math.round(tokens / 1000)}K` : `${tokens}`;
}

export default function ModelSelector() {
  const { t } = useI18n();
  const { state, dispatch } = useAppState();
  const models = state.metadata?.models ?? [];
  const selected = models.find((m) => m.name === state.selectedModel) ?? models[0];
  const provider = formatProvider(selected?.provider ?? state.metadata?.provider);
  const tokenWindow = formatTokenWindow(selected?.max_context_tokens);

  return (
    <div className={styles.section}>
      <div className={styles.label}>{t('sidebar.model')}</div>
      <select
        className={styles.select}
        value={state.selectedModel}
        onChange={(e) => dispatch({ type: 'SET_MODEL', model: e.target.value })}
        data-testid="model-select"
        aria-label="Select model"
      >
        {models.map((m) => (
          <option key={m.name} value={m.name}>
            {m.name}
          </option>
        ))}
      </select>
      <div className={styles.metaLine}>
        <span>{provider}</span>
        <span>
          {t('sidebar.tokenWindow').replace('{count}', tokenWindow)}
        </span>
      </div>
    </div>
  );
}
