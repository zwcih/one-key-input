import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Tauri uses a fixed port and ignores changes that aren't HMR.
export default defineConfig({
  plugins: [react()],
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
  },
  build: {
    target: ["es2021", "chrome105", "safari13"],
  },
});
