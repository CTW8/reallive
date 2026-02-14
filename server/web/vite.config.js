import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

const target = process.env.VITE_PROXY_TARGET || 'http://localhost:3000'

export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': target,
      '/live': target,
      '/history': target,
      '/history-files': target,
      '/ws': {
        target,
        ws: true,
      },
      '/socket.io': {
        target,
        ws: true,
      },
    },
  },
})
