import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, waitFor, act } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { defaultConfig } from "../config";

// `invoke` lives in `@tauri-apps/api/core` and reaches into the Tauri
// IPC bridge, which doesn't exist in jsdom. Mock the whole module so the
// component can be exercised offline.
const invokeMock = vi.fn();
vi.mock("@tauri-apps/api/core", () => ({
  invoke: (...args: unknown[]) => invokeMock(...args),
}));

// Asset imports resolve to URL strings via Vite's asset pipeline by default
// in Vitest, so logo PNG imports just work — no explicit mock needed.

import App from "../App";

beforeEach(() => {
  invokeMock.mockReset();
  invokeMock.mockImplementation((cmd: string) => {
    if (cmd === "load_config") {
      // Provide a fully-populated config so the form path renders.
      const c = defaultConfig();
      c.asr.provider_options.key = "real-key";
      c.polish.provider_options.key = "real-oai-key";
      return Promise.resolve(c);
    }
    if (cmd === "save_config") return Promise.resolve();
    if (cmd === "test_credentials") return Promise.resolve([]);
    if (cmd === "start_core") return Promise.resolve();
    return Promise.reject(new Error("unmocked invoke: " + cmd));
  });
  localStorage.clear();
});

afterEach(() => {
  localStorage.clear();
});

describe("<App />", () => {
  it("loads the config on mount via the Tauri load_config command", async () => {
    render(<App />);
    await waitFor(() => {
      expect(invokeMock).toHaveBeenCalledWith("load_config");
    });
  });

  it("renders the header language switcher after load_config resolves", async () => {
    render(<App />);
    await waitFor(() => {
      expect(screen.getByRole("button", { name: "中文" })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: "EN" })).toBeInTheDocument();
    });
  });

  it("switches language and persists the choice to localStorage", async () => {
    const user = userEvent.setup();
    render(<App />);
    await waitFor(() => screen.getByRole("button", { name: "EN" }));
    await user.click(screen.getByRole("button", { name: "EN" }));

    // The persistence side-effect runs in a useEffect after the click.
    await waitFor(() => {
      expect(localStorage.getItem("onekey.lang")).toBe("en");
    });
    // And document.title is updated to the English app title.
    await waitFor(() => {
      expect(document.title).toMatch(/Settings/i);
    });
  });

  it("shows the loading placeholder while load_config is pending", async () => {
    let resolveCfg!: (v: unknown) => void;
    invokeMock.mockImplementation((cmd: string) => {
      if (cmd === "load_config") {
        return new Promise((res) => { resolveCfg = res; });
      }
      return Promise.resolve();
    });
    render(<App />);
    // The placeholder text is "…" — verify the body section renders without
    // any form fields yet.
    expect(screen.queryByRole("button", { name: "中文" })).not.toBeInTheDocument();
    // Resolve so we don't leak a pending promise into other tests.
    await act(async () => {
      resolveCfg(defaultConfig());
      await Promise.resolve();
    });
  });

  it("renders an error message when load_config rejects", async () => {
    invokeMock.mockImplementation((cmd: string) => {
      if (cmd === "load_config") return Promise.reject("boom");
      return Promise.resolve();
    });
    render(<App />);
    // The error text contains the load-failed prefix from one of the dicts.
    await waitFor(() => {
      const node = document.querySelector(".intro");
      expect(node?.textContent ?? "").toMatch(/boom/);
    });
  });
});
