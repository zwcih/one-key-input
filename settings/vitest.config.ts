/// <reference types="vitest" />
import { defineConfig, mergeConfig } from "vitest/config";
import viteConfig from "./vite.config";

// Vitest config is layered on top of the Vite config so tests use the same
// React/JSX/asset pipeline as the app. Tauri APIs are mocked per-test via
// vi.mock() so this config doesn't have to special-case them.
export default mergeConfig(
  viteConfig,
  defineConfig({
    test: {
      environment: "jsdom",
      globals: true,
      setupFiles: ["./src/__tests__/setup.ts"],
      include: ["src/**/*.{test,spec}.{ts,tsx}"],
    },
  }),
);
