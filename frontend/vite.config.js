import path from 'path';
import { fileURLToPath } from 'url';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
      'next/dynamic': path.resolve(__dirname, './src/shims/next-dynamic.jsx'),
      'next/image': path.resolve(__dirname, './src/shims/next-image.jsx'),
      'next/link': path.resolve(__dirname, './src/shims/next-link.jsx'),
      'next/router': path.resolve(__dirname, './src/shims/next-router.js'),
      'next/navigation': path.resolve(__dirname, './src/shims/next-navigation.js'),
    },
  },
  define: {
    'process.env': {},
  },
  server: {
    port: 5173,
    open: true,
  },
});
