import { useId, type KeyboardEvent, type ReactNode } from 'react';
import styles from './ResourceList.module.css';

export type ResourceListProps<T> = {
  items: readonly T[];
  selectedId: string | null;
  getId: (item: T) => string;
  renderItem: (item: T) => ReactNode;
  onSelect: (id: string) => void;
  ariaLabel?: string;
  className?: string;
};

export default function ResourceList<T>({
  items,
  selectedId,
  getId,
  renderItem,
  onSelect,
  ariaLabel = 'Resources',
  className,
}: ResourceListProps<T>) {
  const listId = useId();
  const selectedIndex = items.findIndex((item) => getId(item) === selectedId);

  const handleKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (items.length === 0) return;

    let nextIndex: number | null = null;

    switch (event.key) {
      case 'ArrowDown':
        nextIndex = selectedIndex < 0 ? 0 : Math.min(selectedIndex + 1, items.length - 1);
        break;
      case 'ArrowUp':
        nextIndex = selectedIndex < 0 ? items.length - 1 : Math.max(selectedIndex - 1, 0);
        break;
      case 'Home':
        nextIndex = 0;
        break;
      case 'End':
        nextIndex = items.length - 1;
        break;
      default:
        return;
    }

    event.preventDefault();
    onSelect(getId(items[nextIndex]));
  };

  return (
    <div
      className={[styles.list, className].filter(Boolean).join(' ')}
      role="listbox"
      aria-label={ariaLabel}
      aria-activedescendant={selectedIndex >= 0 ? `${listId}-option-${selectedIndex}` : undefined}
      tabIndex={0}
      onKeyDown={handleKeyDown}
    >
      {items.map((item, index) => {
        const id = getId(item);
        const selected = id === selectedId;
        return (
          <div
            className={`${styles.option} ${selected ? styles.selected : ''}`}
            id={`${listId}-option-${index}`}
            key={id}
            role="option"
            aria-selected={selected}
            onClick={() => onSelect(id)}
          >
            {renderItem(item)}
          </div>
        );
      })}
    </div>
  );
}
