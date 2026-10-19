import react from '@vitejs/plugin-react';
import { defineConfig, loadEnv } from 'vite';
import { createDevProxy, resolveUpstreams } from './config/devProxy.ts';

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  // Load every variable (not only VITE_*) from .env files and the process environment
  // (process.env wins). The upstream URLs are consumed here only; client code never
  // references them, so they are not embedded in the bundle.
  const env = loadEnv(mode, process.cwd(), '');
  const proxy = createDevProxy(resolveUpstreams(env));
  const devPort = Number(env.VITE_DEV_PORT) || 5173;

  return {
    plugins: [react()],
    server: {
      port: devPort,
      proxy,
    },
    preview: {
      port: 4173,
      proxy,
    },
    build: {
      target: 'es2022',
      // Warn if any single chunk grows beyond ~250 kB minified.
      chunkSizeWarningLimit: 250,
      rolldownOptions: {
        output: {
          // Long-lived vendor chunks (cache well across deploys); every page is its own chunk
          // through React.lazy, and d3 only loads with the graph.
          codeSplitting: {
            groups: [
              {
                name: 'react',
                test: /[\\/]node_modules[\\/](react|react-dom|scheduler|react-router|cookie)[\\/]/,
                priority: 30,
              },
              { name: 'query', test: /[\\/]node_modules[\\/]@tanstack[\\/]/, priority: 20 },
              { name: 'd3', test: /[\\/]node_modules[\\/]d3-[^\\/]+[\\/]/, priority: 10 },
            ],
          },
        },
      },
    },
  };
});
