import { createContext, useCallback, useContext, useState, type ReactNode } from 'react';
import styles from './Toast.module.css';

type ToastType = 'error' | 'success' | 'info';

interface ToastItem {
  id: number;
  message: string;
  type: ToastType;
  removing: boolean;
}

interface ToastContextValue {
  showToast: (message: string, type?: ToastType) => void;
}

const ToastContext = createContext<ToastContextValue | null>(null);

let nextId = 1;

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<ToastItem[]>([]);

  const removeToast = useCallback((id: number) => {
    setToasts((prev) => prev.map((t) => (t.id === id ? { ...t, removing: true } : t)));
    setTimeout(() => {
      setToasts((prev) => prev.filter((t) => t.id !== id));
    }, 300);
  }, []);

  const showToast = useCallback(
    (message: string, type: ToastType = 'error') => {
      const id = nextId++;
      setToasts((prev) => [...prev, { id, message, type, removing: false }]);
      setTimeout(() => removeToast(id), 4000);
    },
    [removeToast],
  );

  return (
    <ToastContext.Provider value={{ showToast }}>
      {children}
      <div className={styles.container} data-testid="toast-container">
        {toasts.map((t) => (
          <div
            key={t.id}
            className={`${styles.toast} ${styles[t.type]} ${t.removing ? styles.removing : ''}`}
            data-testid="toast-item"
          >
            {t.message}
          </div>
        ))}
      </div>
    </ToastContext.Provider>
  );
}

export function useToast(): ToastContextValue {
  const ctx = useContext(ToastContext);
  if (!ctx) throw new Error('useToast must be used within ToastProvider');
  return ctx;
}
