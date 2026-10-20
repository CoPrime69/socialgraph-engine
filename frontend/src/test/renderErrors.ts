import { onTestFinished, vi } from 'vitest';

/**
 * For tests whose component throws on purpose: React reports render errors through the window
 * "error" event and the console. Cancelling the event and muting the console for the rest of the
 * test keeps the output readable. Returns the console spy.
 */
export function allowRenderErrors() {
  const cancel = (event: ErrorEvent) => {
    event.preventDefault();
  };
  window.addEventListener('error', cancel);
  onTestFinished(() => {
    window.removeEventListener('error', cancel);
  });
  return vi.spyOn(console, 'error').mockImplementation(() => undefined);
}
