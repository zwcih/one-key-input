import { describe, it, expect } from "vitest";
import { DICTS, zh, en, Dict, Lang } from "../i18n/dicts";

describe("i18n dicts", () => {
  it("exposes both Chinese and English dictionaries via DICTS", () => {
    const langs: Lang[] = ["zh", "en"];
    for (const l of langs) {
      expect(DICTS[l]).toBeDefined();
    }
  });

  it("zh and en define the same set of keys", () => {
    const zhKeys = Object.keys(zh).sort();
    const enKeys = Object.keys(en).sort();
    expect(enKeys).toEqual(zhKeys);
  });

  it("every value in both dicts is a non-empty string", () => {
    for (const [lang, dict] of Object.entries(DICTS) as [Lang, Dict][]) {
      for (const [key, val] of Object.entries(dict)) {
        expect(typeof val, `${lang}.${key} should be a string`).toBe("string");
        expect((val as string).length, `${lang}.${key} should not be empty`)
          .toBeGreaterThan(0);
      }
    }
  });

  it("zh and en differ on at least one user-facing key", () => {
    // Sanity check we didn't accidentally point both Lang slots at the
    // same dict.
    expect(zh.appTitle).not.toBe(en.appTitle);
  });

  it("polish mode labels differ across languages", () => {
    expect(zh.polishModeRaw).not.toBe(en.polishModeRaw);
    expect(zh.polishModeTidy).not.toBe(en.polishModeTidy);
    expect(zh.polishModeFormal).not.toBe(en.polishModeFormal);
  });
});
