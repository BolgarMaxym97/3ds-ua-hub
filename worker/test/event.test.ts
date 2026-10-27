import { SELF } from "cloudflare:test";
import { env } from "cloudflare:workers";
import { beforeEach, describe, expect, it } from "vitest";

const URL = "https://hub.example/v1/event";

// What app/source/stats.c sends after installing Mario on a game picked by hand.
const install = {
  v: 1,
  id: "K7QM29XD",
  event: "install",
  app: "1.0.0",
  model: "New 3DS XL",
  system: "11.17.0-50E",
  luma: "13.4",
  region: "EUR",
  slug: "super-mario-3d-land",
  tv: "1.0.0",
  vregion: "EUR",
  tid: "0004000000F00D00",
  kind: "patch",
  manual: true,
  risky: false,
  ms: 34000,
  log: "check 0.4s download 29.1s verify 0.3s copy 3.9s",
};

function post(body: unknown) {
  return SELF.fetch(URL, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: typeof body === "string" ? body : JSON.stringify(body),
  });
}

async function rows() {
  const { results } = await env.DB.prepare("SELECT * FROM events ORDER BY id").all();
  return results;
}

describe("POST /v1/event", () => {
  beforeEach(async () => {
    await env.DB.exec("DELETE FROM events");
  });

  it("stores an install with every field", async () => {
    const response = await post(install);

    expect(response.status).toBe(204);
    const [row] = await rows();
    expect(row).toMatchObject({
      install_id: "K7QM29XD",
      event: "install",
      slug: "super-mario-3d-land",
      translation_version: "1.0.0",
      variant_region: "EUR",
      title_id: "0004000000F00D00",
      kind: "patch",
      manual: 1,
      risky: 0,
      duration_ms: 34000,
    });
    expect(row.received_at).toMatch(/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/);
  });

  it("stores an error with its code and stage", async () => {
    await post({ v: 1, id: "K7QM29XD", event: "error", app: "1.0.0", slug: "x", code: "GAME_VERSION", stage: "check" });

    const [row] = await rows();
    expect(row).toMatchObject({ event: "error", error_code: "GAME_VERSION", error_stage: "check", manual: null });
  });

  it("rejects what the app never sends", async () => {
    const bad: [unknown, string][] = [
      ["{", "bad json"],
      [{ ...install, v: 2 }, "unknown version"],
      [{ ...install, id: "k7qm29xd" }, "bad id"],
      [{ ...install, event: "reinstall" }, "bad event"],
      [{ ...install, tid: "0004000000053f00" }, "bad title_id"],
      [{ ...install, kind: "zip" }, "bad kind"],
      [{ ...install, manual: 1 }, "bad manual"],
      [{ ...install, log: "x".repeat(2049) }, "bad log"],
    ];
    for (const [body, error] of bad) {
      const response = await post(body);
      expect(response.status, error).toBe(400);
      expect(await response.text()).toBe(error);
    }
    expect(await rows()).toEqual([]);
  });

  it("refuses an oversized body", async () => {
    const response = await post({ ...install, log: "x".repeat(5000) });

    expect(response.status).toBe(413);
  });
});

describe("other routes", () => {
  it("answers health", async () => {
    const response = await SELF.fetch("https://hub.example/v1/health");
    expect(await response.text()).toBe("ok");
  });

  it("says 404 elsewhere", async () => {
    expect((await SELF.fetch("https://hub.example/")).status).toBe(404);
  });
});
