import { Component, type ReactNode } from 'react';

interface Props {
  children: ReactNode;
  fallback?: ReactNode;
}

interface State {
  error: Error | null;
}

export default class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  render() {
    if (this.state.error) {
      return (
        this.props.fallback ?? (
          <div
            role="alert"
            style={{
              padding: 24,
              margin: 16,
              background: 'var(--ruby-soft)',
              border: '1px solid rgba(190, 18, 60, 0.22)',
              borderRadius: 8,
              color: 'var(--ruby)',
              fontSize: 13,
            }}
            data-testid="error-fallback"
          >
            <strong>Something went wrong</strong>
            <pre
              style={{
                marginTop: 8,
                fontSize: 11,
                color: 'var(--muted-strong)',
                whiteSpace: 'pre-wrap',
                wordBreak: 'break-all',
              }}
            >
              {this.state.error.message}
            </pre>
            <button
              onClick={() => this.setState({ error: null })}
              style={{
                marginTop: 10,
                background: 'var(--ruby)',
                color: '#fff',
                border: 'none',
                borderRadius: 4,
                padding: '4px 14px',
                cursor: 'pointer',
                fontSize: 12,
                fontWeight: 600,
              }}
              data-testid="error-dismiss"
              aria-label="Dismiss error"
            >
              Dismiss
            </button>
          </div>
        )
      );
    }
    return this.props.children;
  }
}
