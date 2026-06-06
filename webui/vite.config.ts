import { defineConfig } from 'vite';

const apiProxyTarget = process.env.VITE_PROXY_TARGET ?? 'http://127.0.0.1:3888';

export default defineConfig({
  server: {
    port: 5173,
    proxy: {
      '/v1': apiProxyTarget,
      '/api': apiProxyTarget,
    },
  },
  build: {
    outDir: 'dist',
    chunkSizeWarningLimit: 700,
    rollupOptions: {
      onwarn(warning, defaultHandler) {
        if (
          warning.code === 'MODULE_LEVEL_DIRECTIVE' &&
          typeof warning.message === 'string' &&
          warning.message.includes('lucide-react')
        ) {
          return;
        }
        defaultHandler(warning);
      },
    },
  },
  test: {
    environment: 'jsdom',
    globals: true,
  },
});
