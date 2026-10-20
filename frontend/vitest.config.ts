import { defineConfig, mergeConfig } from 'vitest/config';
import viteConfig from './vite.config.ts';

// The app formats numbers and dates in the viewer's locale and time zone. Both are pinned for the
// test workers (which inherit this process's environment) so that assertions such as "18,649"
// or "Sep 2008" hold on every machine.
process.env.LC_ALL = 'en_US.UTF-8';
process.env.TZ = 'UTC';

export default defineConfig((configEnv) =>
  mergeConfig(
    viteConfig(configEnv),
    defineConfig({
      test: {
        environment: 'jsdom',
        setupFiles: ['./src/test/setup.ts'],
        include: ['src/**/*.test.{ts,tsx}', 'config/**/*.test.ts'],
        restoreMocks: true,
        env: { TZ: 'UTC', LC_ALL: 'en_US.UTF-8' },
        testTimeout: 15_000,
        coverage: {
          provider: 'v8',
          include: ['src/**/*.{ts,tsx}'],
          exclude: ['src/**/*.test.{ts,tsx}', 'src/test/**', 'src/main.tsx', 'src/**/*.d.ts'],
          reporter: ['text', 'text-summary', 'html', 'json-summary'],
          thresholds: {
            statements: 80,
            branches: 75,
            functions: 80,
            lines: 80,
          },
        },
      },
    }),
  ),
);
