import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'jsdom',
    setupFiles: ['./vitest.setup.ts', './node_modules/@testing-library/jest-dom/vitest.js'],
  },
});
