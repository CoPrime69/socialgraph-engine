import { setupServer } from 'msw/node';
import { handlers } from './mockBackend';

/** MSW server for every test; individual tests prepend overrides with `server.use(...)`. */
export const server = setupServer(...handlers);
