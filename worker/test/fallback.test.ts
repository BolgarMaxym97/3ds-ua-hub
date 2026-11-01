import { SELF } from "cloudflare:test";
import { env } from "cloudflare:workers";
import { beforeEach, describe, expect, it } from "vitest";
import { serveFallback, type Upstream } from "../src/fallback";

const SHA = (c: string) => c.repeat(64);
const GAME = new Uint8Array(1000).map((_, i) => i % 251);
const LINK = "https://games.example/mario.cia";

async function seed({ fallback = true, kind = "patch", published = 1 } = {}) {
  await env.DB.exec("DELETE FROM screenshots; DELETE FROM variants; DELETE FROM translations;");
  await env.DB.batch([
    env.DB.prepare(
      `INSERT INTO translations (slug, title, kind, version, published)
       VALUES ('super-mario-3d-land', 'Super Mario 3D Land', ?, '1.0.0', ?)`,
    ).bind(kind, published),
    env.DB.prepare(
      `INSERT INTO variants (slug, region, title_id, product_code, file_key, file_size, file_sha256,
                             fallback_url, fallback_size, fallback_sha256)
       VALUES ('super-mario-3d-land', 'EUR', '0004000000053F00', 'CTR-P-AREP',
               'super-mario-3d-land/patch/1.0.0/eur.zip', 69632, ?, ?, ?, ?)`,
    ).bind(SHA("e"), fallback ? LINK : null, fallback ? GAME.length : null, fallback ? SHA("c") : null),
    env.DB.prepare(
      `INSERT INTO variants (slug, region, title_id, product_code, file_key, file_size, file_sha256)
       VALUES ('super-mario-3d-land', 'USA', '0004000000054000', 'CTR-P-AREE',
               'super-mario-3d-land/patch/1.0.0/usa.zip', 69632, ?)`,
    ).bind(SHA("a")),
  ]);
}

// A server that holds GAME at LINK and answers Range as servers do; `twist` changes its answer.
function server(twist: (init: RequestInit) => Response | null = () => null) {
  const calls: { url: string; range: string | null }[] = [];
  const upstream: Upstream = async (url, init) => {
    const range = new Headers(init.headers).get("Range");
    calls.push({ url, range });
    const twisted = twist(init);
    if (twisted) return twisted;
    if (url !== LINK) return new Response("nope", { status: 404, headers: { "Content-Type": "text/plain" } });
    const match = range ? /^bytes=(\d+)-(\d*)$/.exec(range) : null;
    if (!match) {
      return new Response(GAME, { headers: { "Content-Type": "application/octet-stream", "Content-Length": "1000" } });
    }
    const from = Number(match[1]);
    if (from >= GAME.length) return new Response(null, { status: 416 });
    const to = match[2] === "" ? GAME.length - 1 : Number(match[2]);
    return new Response(GAME.slice(from, to + 1), {
      status: 206,
      headers: {
        "Content-Type": "application/octet-stream",
        "Content-Length": String(to - from + 1),
        "Content-Range": `bytes ${from}-${to}/${GAME.length}`,
      },
    });
  };
  return { upstream, calls };
}

const URL_EUR = "https://hub.example/v1/fallback/super-mario-3d-land/0004000000053F00";

function ask(method: string, upstream: Upstream, headers: Record<string, string> = {}, tid = "0004000000053F00") {
  return serveFallback(new Request(URL_EUR, { method, headers }), env.DB, "super-mario-3d-land", tid, upstream);
}

describe("fallback columns", () => {
  beforeEach(() => seed({ fallback: false }));

  it("refuses a link without its size and sha256", async () => {
    await expect(
      env.DB.prepare(`UPDATE variants SET fallback_url = ? WHERE region = 'EUR'`).bind(LINK).run(),
    ).rejects.toThrow(/go together/);
  });

  it("refuses a link for a patch that needs a game update", async () => {
    await expect(
      env.DB.prepare(
        `UPDATE variants SET game_version = 1040, fallback_url = ?, fallback_size = 1000, fallback_sha256 = ?
         WHERE region = 'EUR'`,
      ).bind(LINK, SHA("c")).run(),
    ).rejects.toThrow(/above 0/);
  });

  it("takes a link for a patch of the game without updates (game_version 0)", async () => {
    await env.DB.prepare(
      `UPDATE variants SET game_version = 0, fallback_url = ?, fallback_size = 1000, fallback_sha256 = ?
       WHERE region = 'EUR'`,
    ).bind(LINK, SHA("c")).run();
    const row = await env.DB.prepare(`SELECT fallback_url FROM variants WHERE region = 'EUR'`).first<{ fallback_url: string }>();
    expect(row?.fallback_url).toBe(LINK);
  });

  it("refuses a link that is not http(s)", async () => {
    await expect(
      env.DB.prepare(
        `UPDATE variants SET fallback_url = 'ftp://x/y.cia', fallback_size = 1000, fallback_sha256 = ? WHERE region = 'EUR'`,
      ).bind(SHA("c")).run(),
    ).rejects.toThrow();
  });
});

describe("the catalog", () => {
  it("lists the game's size and sha256, never the link", async () => {
    await seed();
    const body = await (await SELF.fetch("https://hub.example/v1/catalog")).text();
    const [eur, usa] = JSON.parse(body).items[0].variants;

    expect(eur.fallback).toEqual({ size: 1000, sha256: SHA("c") });
    expect(usa.fallback).toBeUndefined();
    expect(body).not.toContain("games.example");
  });

  it("leaves it out of a CIA translation", async () => {
    await seed({ kind: "cia" });
    const body = await (await SELF.fetch("https://hub.example/v1/catalog")).json<{ items: { variants: object[] }[] }>();
    expect(body.items[0].variants[0]).not.toHaveProperty("fallback");
  });
});

describe("qr_url", () => {
  it("goes into the catalog as qr, for a patch only", async () => {
    await seed();
    await env.DB.prepare(`UPDATE variants SET qr_url = 'https://games.example/p/mario' WHERE region = 'USA'`).run();
    const body = await (await SELF.fetch("https://hub.example/v1/catalog")).json<{ items: { variants: Record<string, unknown>[] }[] }>();
    expect(body.items[0].variants[1].qr).toBe("https://games.example/p/mario");
    expect(body.items[0].variants[0]).not.toHaveProperty("qr");

    await env.DB.prepare(`UPDATE translations SET kind = 'cia'`).run();
    const cia = await (await SELF.fetch("https://hub.example/v1/catalog")).json<{ items: { variants: Record<string, unknown>[] }[] }>();
    expect(cia.items[0].variants[1]).not.toHaveProperty("qr");
  });

  it.each([
    ["not a web address", "ftp://games.example/x"],
    ["spaces", "https://games.example/a b"],
    ["letters the QR text cannot carry plainly", "https://games.example/гра"],
    ["over 200 characters", `https://games.example/${"a".repeat(190)}`],
  ])("refuses %s", async (_, url) => {
    await seed();
    await expect(env.DB.prepare(`UPDATE variants SET qr_url = ? WHERE region = 'USA'`).bind(url).run()).rejects.toThrow();
  });
});

describe("HEAD /v1/fallback/<slug>/<TID>", () => {
  beforeEach(() => seed());

  it("says the link works, with the game's size, from its first byte", async () => {
    const { upstream, calls } = server();
    const response = await ask("HEAD", upstream);

    expect(response.status).toBe(200);
    expect(response.headers.get("Content-Length")).toBe("1000");
    expect(calls).toEqual([{ url: LINK, range: "bytes=0-0" }]);
  });

  it("takes a server that ignores Range", async () => {
    const { upstream } = server(() =>
      new Response(GAME, { headers: { "Content-Type": "application/octet-stream", "Content-Length": "1000" } }),
    );
    expect((await ask("HEAD", upstream)).status).toBe(200);
  });

  it.each([
    ["the file is gone", () => new Response("gone", { status: 404 }), "status-404"],
    ["access is refused", () => new Response("no", { status: 403 }), "status-403"],
    ["a page comes instead of the file", () => new Response("<html>", { headers: { "Content-Type": "text/html; charset=utf-8" } }), "html"],
    [
      "the file is another size",
      () => new Response("x", { status: 206, headers: { "Content-Range": "bytes 0-0/2000", "Content-Type": "application/octet-stream" } }),
      "size-2000",
    ],
  ])("says what is wrong when %s", async (_, answer, reason) => {
    const { upstream } = server(answer);
    const response = await ask("HEAD", upstream);

    expect(response.status).toBe(502);
    expect(response.headers.get("X-Fallback-Error")).toBe(reason);
  });

  it("says unreachable when the server does not answer", async () => {
    const response = await ask("HEAD", async () => {
      throw new TypeError("fetch failed");
    });

    expect(response.status).toBe(502);
    expect(response.headers.get("X-Fallback-Error")).toBe("unreachable");
  });

  it("follows no link outside the catalog", async () => {
    const { upstream, calls } = server();

    expect((await ask("HEAD", upstream, {}, "0004000000054000")).status).toBe(404); // USA has no link
    expect((await ask("HEAD", upstream, {}, "0004000000099900")).status).toBe(404);
    expect(calls).toEqual([]);
  });

  it("follows no link of an unpublished translation", async () => {
    await seed({ published: 0 });
    const { upstream } = server();
    expect((await ask("HEAD", upstream)).status).toBe(404);
  });
});

describe("GET /v1/fallback/<slug>/<TID>", () => {
  beforeEach(() => seed());

  it("hands out the whole game", async () => {
    const response = await ask("GET", server().upstream);

    expect(response.status).toBe(200);
    expect(response.headers.get("Cache-Control")).toBe("no-store");
    expect(new Uint8Array(await response.arrayBuffer())).toEqual(GAME);
  });

  it("resumes from a byte offset", async () => {
    const { upstream, calls } = server();
    const response = await ask("GET", upstream, { Range: "bytes=400-" });

    expect(calls[0].range).toBe("bytes=400-");
    expect(response.status).toBe(206);
    expect(response.headers.get("Content-Range")).toBe("bytes 400-999/1000");
    expect(new Uint8Array(await response.arrayBuffer())).toEqual(GAME.slice(400));
  });

  it("passes the whole file on when the server ignores Range", async () => {
    const { upstream } = server(() =>
      new Response(GAME, { headers: { "Content-Type": "application/octet-stream", "Content-Length": "1000" } }),
    );
    const response = await ask("GET", upstream, { Range: "bytes=400-" });

    expect(response.status).toBe(200);
    expect(response.headers.get("Content-Range")).toBeNull();
    expect((await response.arrayBuffer()).byteLength).toBe(1000);
  });

  it("says 416 when the console has every byte", async () => {
    const response = await ask("GET", server().upstream, { Range: "bytes=1000-" });

    expect(response.status).toBe(416);
    expect(response.headers.get("Content-Range")).toBe("bytes */1000");
  });

  it("says the link broke when the file is gone mid-way", async () => {
    const { upstream } = server(() => new Response("gone", { status: 410 }));
    const response = await ask("GET", upstream, { Range: "bytes=400-" });

    expect(response.status).toBe(502);
    expect(response.headers.get("X-Fallback-Error")).toBe("status-410");
  });

  it("is routed, and refuses other methods", async () => {
    expect((await SELF.fetch(URL_EUR, { method: "POST" })).status).toBe(405);
    expect((await SELF.fetch("https://hub.example/v1/fallback/super-mario-3d-land/0004000000054000")).status).toBe(404);
    expect((await SELF.fetch("https://hub.example/v1/fallback/x/lowercase0000000")).status).toBe(404);
  });
});
