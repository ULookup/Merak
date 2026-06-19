import { useState } from 'react';
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import DetailPane from '../components/layout/DetailPane';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';

type Deferred<T> = {
  promise: Promise<T>;
  resolve(value: T): void;
  reject(error: unknown): void;
};

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

function ResourceHarness({
  resourceKey,
  loader,
  onRender,
}: {
  resourceKey: string;
  loader: (signal: AbortSignal) => Promise<string>;
  onRender?: (key: string, data: string | null) => void;
}) {
  const resource = useResource(resourceKey, loader);
  onRender?.(resourceKey, resource.data);
  return (
    <div>
      <output aria-label="status">{resource.status}</output>
      <output aria-label="data">{resource.data ?? 'none'}</output>
      <output aria-label="error">{resource.error?.message ?? 'none'}</output>
      <button type="button" onClick={resource.retry}>
        Retry resource
      </button>
    </div>
  );
}

describe('useResource', () => {
  it('does not expose data from the previous key during the new key render', async () => {
    const renders: Array<{ key: string; data: string | null }> = [];
    const loader = vi
      .fn()
      .mockResolvedValueOnce('first data')
      .mockReturnValue(new Promise(() => {}));
    const onRender = (key: string, data: string | null) => renders.push({ key, data });
    const view = render(
      <ResourceHarness resourceKey="first" loader={loader} onRender={onRender} />,
    );

    await waitFor(() => expect(screen.getByLabelText('data')).toHaveTextContent('first data'));
    const renderCountBeforeKeyChange = renders.length;
    view.rerender(<ResourceHarness resourceKey="second" loader={loader} onRender={onRender} />);

    expect(renders[renderCountBeforeKeyChange]).toEqual({ key: 'second', data: null });
  });

  it('aborts the previous request when the resource key changes', async () => {
    const first = deferred<string>();
    const second = deferred<string>();
    const signals: AbortSignal[] = [];
    const loader = vi.fn((signal: AbortSignal) => {
      signals.push(signal);
      return signals.length === 1 ? first.promise : second.promise;
    });
    const view = render(<ResourceHarness resourceKey="first" loader={loader} />);

    await waitFor(() => expect(loader).toHaveBeenCalledTimes(1));
    view.rerender(<ResourceHarness resourceKey="second" loader={loader} />);

    await waitFor(() => expect(loader).toHaveBeenCalledTimes(2));
    expect(signals[0].aborted).toBe(true);
    expect(screen.getByLabelText('data')).toHaveTextContent('none');

    await act(async () => second.resolve('second data'));
    expect(screen.getByLabelText('data')).toHaveTextContent('second data');
  });

  it('retains successful data when a retry fails', async () => {
    const retryRequest = deferred<string>();
    const loader = vi
      .fn<(signal: AbortSignal) => Promise<string>>()
      .mockResolvedValueOnce('retained data')
      .mockImplementationOnce(() => retryRequest.promise);

    render(<ResourceHarness resourceKey="resource" loader={loader} />);
    await waitFor(() => expect(screen.getByLabelText('status')).toHaveTextContent('ready'));

    fireEvent.click(screen.getByRole('button', { name: 'Retry resource' }));
    expect(screen.getByLabelText('status')).toHaveTextContent('loading');
    expect(screen.getByLabelText('data')).toHaveTextContent('retained data');

    await act(async () => retryRequest.reject(new Error('Refresh failed')));
    expect(screen.getByLabelText('status')).toHaveTextContent('error');
    expect(screen.getByLabelText('data')).toHaveTextContent('retained data');
    expect(screen.getByLabelText('error')).toHaveTextContent('Refresh failed');
  });

  it('ignores an aborted request rejection', async () => {
    const request = deferred<string>();
    render(<ResourceHarness resourceKey="resource" loader={() => request.promise} />);

    await act(async () => request.reject(new DOMException('Aborted', 'AbortError')));

    expect(screen.getByLabelText('status')).toHaveTextContent('loading');
    expect(screen.getByLabelText('error')).toHaveTextContent('none');
  });
});

describe('PageState', () => {
  it('shows a non-blocking warning while retaining stale content after refresh failure', () => {
    const retry = vi.fn();
    render(
      <PageState error={new Error('Refresh failed')} hasData onRetry={retry}>
        <p>Retained content</p>
      </PageState>,
    );

    expect(screen.getByRole('alert')).toHaveTextContent('Refresh failed');
    expect(screen.getByText('Retained content')).toBeDefined();
    fireEvent.click(screen.getByRole('button', { name: 'Retry' }));
    expect(retry).toHaveBeenCalledOnce();
  });

  it('renders loading, full error, and empty states without page-specific copy', () => {
    const { rerender } = render(<PageState loading loadingLabel="Loading worlds" />);
    expect(screen.getByRole('status')).toHaveAccessibleName('Loading worlds');

    rerender(<PageState error={new Error('Worlds unavailable')} onRetry={() => {}} />);
    expect(screen.getByRole('alert')).toHaveTextContent('Worlds unavailable');

    rerender(<PageState isEmpty emptyTitle="No worlds" emptyDescription="Create one to begin." />);
    expect(screen.getByRole('heading', { name: 'No worlds' })).toBeDefined();
    expect(screen.getByText('Create one to begin.')).toBeDefined();
  });
});

describe('ResourceList', () => {
  const items = [
    { id: 'a', name: 'A' },
    { id: 'b', name: 'B' },
  ];

  function SelectableList({ initialId = 'a' }: { initialId?: string }) {
    const [selectedId, setSelectedId] = useState(initialId);
    return (
      <ResourceList
        items={items}
        selectedId={selectedId}
        getId={(item) => item.id}
        renderItem={(item) => item.name}
        onSelect={setSelectedId}
      />
    );
  }

  it('scrolls keyboard-selected options into view', () => {
    const scrollIntoView = vi.fn();
    Object.defineProperty(HTMLElement.prototype, 'scrollIntoView', {
      configurable: true,
      value: scrollIntoView,
    });

    render(<SelectableList />);
    scrollIntoView.mockClear();
    fireEvent.keyDown(screen.getByRole('listbox'), { key: 'End' });

    expect(screen.getByRole('option', { name: 'B' })).toHaveAttribute('aria-selected', 'true');
    expect(scrollIntoView).toHaveBeenCalledWith({ block: 'nearest' });

    scrollIntoView.mockClear();
    fireEvent.keyDown(screen.getByRole('listbox'), { key: 'Home' });
    expect(screen.getByRole('option', { name: 'A' })).toHaveAttribute('aria-selected', 'true');
    expect(scrollIntoView).toHaveBeenCalledWith({ block: 'nearest' });

    scrollIntoView.mockClear();
    fireEvent.keyDown(screen.getByRole('listbox'), { key: 'ArrowDown' });
    expect(screen.getByRole('option', { name: 'B' })).toHaveAttribute('aria-selected', 'true');
    expect(scrollIntoView).toHaveBeenCalledWith({ block: 'nearest' });

    delete HTMLElement.prototype.scrollIntoView;
  });

  it('focuses the listbox when an option is clicked', () => {
    render(<SelectableList />);

    fireEvent.click(screen.getByRole('option', { name: 'B' }));

    const listbox = screen.getByRole('listbox');
    expect(listbox).toHaveFocus();
    fireEvent.keyDown(listbox, { key: 'ArrowUp' });
    expect(screen.getByRole('option', { name: 'A' })).toHaveAttribute('aria-selected', 'true');
  });

  it('selects the next item with the keyboard and exposes the selected option', () => {
    const onSelect = vi.fn();
    render(
      <ResourceList
        ariaLabel="Characters"
        items={items}
        selectedId="a"
        getId={(item) => item.id}
        renderItem={(item) => item.name}
        onSelect={onSelect}
      />,
    );

    const listbox = screen.getByRole('listbox', { name: 'Characters' });
    expect(screen.getByRole('option', { name: 'A' })).toHaveAttribute('aria-selected', 'true');
    fireEvent.keyDown(listbox, { key: 'ArrowDown' });
    expect(onSelect).toHaveBeenCalledWith('b');
  });

  it('supports boundary navigation and mouse selection', () => {
    const onSelect = vi.fn();
    render(
      <ResourceList
        items={items}
        selectedId="b"
        getId={(item) => item.id}
        renderItem={(item) => item.name}
        onSelect={onSelect}
      />,
    );

    const listbox = screen.getByRole('listbox');
    fireEvent.keyDown(listbox, { key: 'ArrowDown' });
    expect(onSelect).toHaveBeenLastCalledWith('b');
    fireEvent.keyDown(listbox, { key: 'Home' });
    expect(onSelect).toHaveBeenLastCalledWith('a');
    fireEvent.click(screen.getByRole('option', { name: 'B' }));
    expect(onSelect).toHaveBeenLastCalledWith('b');
  });
});

describe('DetailPane', () => {
  it('provides a labeled detail region with actions and an optional inspector', () => {
    render(
      <DetailPane
        title="Lian"
        description="Archivist"
        actions={<button type="button">Edit</button>}
        inspector={<p>Related chapters</p>}
      >
        <p>Character details</p>
      </DetailPane>,
    );

    expect(screen.getByRole('region', { name: 'Lian' })).toBeDefined();
    expect(screen.getByRole('button', { name: 'Edit' })).toBeDefined();
    expect(screen.getByRole('complementary')).toHaveTextContent('Related chapters');
  });
});
