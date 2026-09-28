import path from 'path';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

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
});
