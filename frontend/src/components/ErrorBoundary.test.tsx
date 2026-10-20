import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it } from 'vitest';
import { UnexpectedError } from '../features/errors/RouteErrorPage';
import { allowRenderErrors } from '../test/renderErrors';
import { ErrorBoundary } from './ErrorBoundary';

/** Throws `failure.error` while `failure.active` is set. */
function Widget({ failure }: { failure: { active: boolean; error: unknown } }) {
  if (failure.active) throw failure.error;
  return <p>Widget ready</p>;
}

describe('ErrorBoundary', () => {
  it('replaces a crashed tree with the error page and recovers on "Try again"', async () => {
    const consoleError = allowRenderErrors();
    const failure = { active: true, error: new Error('Widget exploded') as unknown };
    render(
      <ErrorBoundary fallback={(error, reset) => <UnexpectedError error={error} onReset={reset} />}>
        <Widget failure={failure} />
      </ErrorBoundary>,
    );

    expect(
      screen.getByRole('heading', { level: 1, name: 'Something went wrong' }),
    ).toBeInTheDocument();
    expect(screen.getByRole('main')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Reload' })).toBeInTheDocument();
    expect(consoleError).toHaveBeenCalledWith(
      'Unhandled UI error',
      failure.error,
      expect.stringContaining('Widget'),
    );

    failure.active = false;
    await userEvent.click(screen.getByRole('button', { name: 'Try again' }));
    expect(screen.getByText('Widget ready')).toBeInTheDocument();
    expect(screen.queryByRole('heading', { name: 'Something went wrong' })).not.toBeInTheDocument();
  });

  it('turns thrown values that are not errors into errors', () => {
    allowRenderErrors();
    render(
      <ErrorBoundary fallback={(error) => <p>Caught: {error.message}</p>}>
        <Widget failure={{ active: true, error: 'plain failure' }} />
      </ErrorBoundary>,
    );
    expect(screen.getByText('Caught: plain failure')).toBeInTheDocument();
  });

  it('renders its children when nothing fails', () => {
    render(
      <ErrorBoundary fallback={() => <p>Fallback</p>}>
        <Widget failure={{ active: false, error: null }} />
      </ErrorBoundary>,
    );
    expect(screen.getByText('Widget ready')).toBeInTheDocument();
  });
});
