import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';

export default defineConfig({
  build: {
    rollupOptions: {
      input: {
        design: fileURLToPath(new URL('index.html', import.meta.url)),
        simulation: fileURLToPath(new URL('simulation.html', import.meta.url))
      }
    }
  }
});
