import { act, fireEvent, render, renderHook, screen, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { allowRenderErrors } from '../test/renderErrors';
import { ToastProvider } from './Toast';
import { useToast } from './toastContext';

function Harness() {
  const toast = useToast();
  return (
    <>
      <button
        type="button"
        onClick={() => {
          toast.success('Connected with Jon Skeet', 'Your network has been updated.');
        }}
      >
        Show success
      </button>
      <button
        type="button"
        onClick={() => {
          toast.error("Couldn't connect");
        }}
      >
        Show error
      </button>
      <button
        type="button"
        onClick={() => {
          toast.info('Already connected');
        }}
      >
        Show info
      </button>
    </>
  );
}

function renderToasts() {
  render(
    <ToastProvider>
      <Harness />
    </ToastProvider>,
  );
  const polite = screen.getByRole('status');
  const assertive = screen.getByRole('alert');
  return { polite, assertive, viewport: polite.parentElement! };
}

function show(kind: 'success' | 'error' | 'info') {
  fireEvent.click(screen.getByRole('button', { name: `Show ${kind}` }));
}

function advance(ms: number) {
  act(() => {
    vi.advanceTimersByTime(ms);
  });
}

describe('Toasts', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('announces successes politely and dismisses them after five seconds', () => {
    const { polite, assertive } = renderToasts();
    show('success');
    expect(within(polite).getByText('Connected with Jon Skeet')).toBeInTheDocument();
    expect(within(polite).getByText('Your network has been updated.')).toBeInTheDocument();
    expect(assertive).toBeEmptyDOMElement();

    advance(4_999);
    expect(screen.getByText('Connected with Jon Skeet')).toBeInTheDocument();
    advance(1);
    expect(screen.queryByText('Connected with Jon Skeet')).not.toBeInTheDocument();
  });

  it('announces errors assertively and keeps them for ten seconds', () => {
    const { polite, assertive } = renderToasts();
    show('error');
    show('info');
    expect(within(assertive).getByText("Couldn't connect")).toBeInTheDocument();
    expect(within(polite).getByText('Already connected')).toBeInTheDocument();

    advance(5_000);
    expect(screen.queryByText('Already connected')).not.toBeInTheDocument();
    expect(screen.getByText("Couldn't connect")).toBeInTheDocument();
    advance(5_000);
    expect(screen.queryByText("Couldn't connect")).not.toBeInTheDocument();
  });

  it('can be dismissed at once', () => {
    renderToasts();
    show('error');
    fireEvent.click(screen.getByRole('button', { name: 'Dismiss notification' }));
    expect(screen.queryByText("Couldn't connect")).not.toBeInTheDocument();
  });

  it('shows at most four at a time, dropping the oldest', () => {
    renderToasts();
    show('error');
    for (let i = 0; i < 4; i++) show('info');
    expect(screen.queryByText("Couldn't connect")).not.toBeInTheDocument();
    expect(screen.getAllByText('Already connected')).toHaveLength(4);
  });

  it('stays while the pointer is over it and for a full period after it leaves', () => {
    const { viewport } = renderToasts();
    show('success');
    fireEvent.mouseEnter(viewport);
    advance(60_000);
    expect(screen.getByText('Connected with Jon Skeet')).toBeInTheDocument();

    // Toasts added while paused wait too.
    show('info');
    advance(60_000);
    expect(screen.getByText('Already connected')).toBeInTheDocument();

    fireEvent.mouseLeave(viewport);
    advance(4_999);
    expect(screen.getByText('Connected with Jon Skeet')).toBeInTheDocument();
    advance(1);
    expect(screen.queryByText('Connected with Jon Skeet')).not.toBeInTheDocument();
    expect(screen.queryByText('Already connected')).not.toBeInTheDocument();
  });

  it('stays while keyboard focus is inside it', () => {
    renderToasts();
    show('success');
    const dismiss = screen.getByRole('button', { name: 'Dismiss notification' });
    fireEvent.focus(dismiss);
    advance(60_000);
    expect(screen.getByText('Connected with Jon Skeet')).toBeInTheDocument();
    fireEvent.blur(dismiss);
    advance(5_000);
    expect(screen.queryByText('Connected with Jon Skeet')).not.toBeInTheDocument();
  });

  it('must be used inside the provider', () => {
    allowRenderErrors();
    expect(() => renderHook(() => useToast())).toThrow(
      'useToast must be used inside <ToastProvider>',
    );
  });
});
