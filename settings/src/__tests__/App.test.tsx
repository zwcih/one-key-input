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

  it("renders the Translation Mode section with default F8 / English", async () => {
    render(<App />);
    // Wait for the form to appear, then assert the new Translate group is present.
    await waitFor(() => screen.getByRole("button", { name: "中文" }));
    // Group heading exists in either zh or en. The default lang depends on
    // navigator.language, so accept either by class lookup.
    const headings = Array.from(document.querySelectorAll("section.group h2"))
      .map((n) => n.textContent ?? "");
    expect(headings.some((h) => /翻译模式|Translation Mode/i.test(h))).toBe(true);

    const hotkeyInput = document.getElementById("tr-hotkey") as HTMLInputElement;
    expect(hotkeyInput.value).toBe("f8");

    const target = document.getElementById("tr-target") as HTMLSelectElement;
    expect(target.value).toBe("en");

    const smart = document.getElementById("tr-smart") as HTMLInputElement;
    expect(smart.checked).toBe(false);
  });

  // Regression test for the v0.1 → v0.2 upgrade white-screen bug:
  // an upgraded user's on-disk config.json has no `translate` section,
  // and the form previously crashed reading `cfg.translate.enabled`.
  // The mount-time backfill must fill in the default translate block
  // so the form renders normally.
  it("backfills a missing translate section from a v0.1 config without white-screening", async () => {
    invokeMock.mockImplementation((cmd: string) => {
      if (cmd === "load_config") {
        // v0.1-shape config: complete except no `translate` key.
        return Promise.resolve({
          asr: {
            provider: "azure-stream",
            language: "zh-CN",
            provider_options: { key: "real-key", region: "westus2" },
          },
          polish: {
            provider: "openai-azure",
            mode: "tidy",
            provider_options: { key: "real-oai", endpoint: "" },
          },
          inject: { mode: "sendinput" },
          hotkey: { key: "f9", min_hold_ms: 250 },
          sound: { enabled: true },
          autostart: { enabled: true },
          // translate intentionally absent
        });
      }
      if (cmd === "save_config") return Promise.resolve();
      if (cmd === "test_credentials") return Promise.resolve([]);
      if (cmd === "start_core") return Promise.resolve();
      return Promise.reject(new Error("unmocked invoke: " + cmd));
    });

    render(<App />);
    // Form must render — header buttons are a reliable signal.
    await waitFor(() =>
      expect(screen.getByRole("button", { name: "中文" })).toBeInTheDocument(),
    );

    // Translate fields must show the documented defaults (F8 / en / smart off).
    const hotkeyInput = document.getElementById("tr-hotkey") as HTMLInputElement;
    expect(hotkeyInput).not.toBeNull();
    expect(hotkeyInput.value).toBe("f8");

    const target = document.getElementById("tr-target") as HTMLSelectElement;
    expect(target.value).toBe("en");

    const enabled = document.getElementById("tr-enabled") as HTMLInputElement;
    expect(enabled.checked).toBe(true);

    const smart = document.getElementById("tr-smart") as HTMLInputElement;
    expect(smart.checked).toBe(false);
  });
});
