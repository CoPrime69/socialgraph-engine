import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import '@fontsource-variable/geist/wght.css';
import './styles/global.css';
import { App } from './app/App';
import { wireApiClient } from './app/wireApiClient';
import { ErrorBoundary } from './components/ErrorBoundary';
import { UnexpectedError } from './features/errors/RouteErrorPage';

wireApiClient();

const container = document.getElementById('root');
if (!container) throw new Error('The #root element is missing from index.html');

createRoot(container).render(
  <StrictMode>
    <ErrorBoundary fallback={(error, reset) => <UnexpectedError error={error} onReset={reset} />}>
      <App />
    </ErrorBoundary>
  </StrictMode>,
);
