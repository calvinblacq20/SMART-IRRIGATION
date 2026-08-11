import { defineConfig } from 'vite';

export default defineConfig({
  server: {
    port: 5173,
    host: true, // Expose to local network (laptop IP & mobile phone testing)
    cors: true
  },
  build: {
    outDir: 'dist',
    assetsDir: 'assets'
  }
});
