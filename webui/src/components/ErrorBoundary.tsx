import { Component, type ErrorInfo, type ReactNode } from 'react';

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

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error('[ErrorBoundary]', error, info.componentStack);
  }

  render() {
    if (this.state.error) {
      return (
        this.props.fallback ?? (
          <div
            style={{
              padding: 24,
              margin: 16,
              background: '#1a0f0f',
              border: '1px solid #4a2020',
              borderRadius: 8,
              color: '#fca5a5',
              fontSize: 13,
            }}
            data-testid="error-fallback"
          >
            <strong>Something went wrong</strong>
            <pre
              style={{
                marginTop: 8,
                fontSize: 11,
                color: '#c8c9d4',
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
                background: '#7f1d1d',
                color: '#fca5a5',
                border: 'none',
                borderRadius: 4,
                padding: '4px 14px',
                cursor: 'pointer',
                fontSize: 12,
                fontWeight: 600,
              }}
              data-testid="error-dismiss"
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
