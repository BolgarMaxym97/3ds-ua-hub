import { SELF } from "cloudflare:test";
import { env } from "cloudflare:workers";
import { beforeEach, describe, expect, it } from "vitest";

const URL = "https://hub.example/v1/catalog";
const SHA = (c: string) => c.repeat(64);

// Super Mario 3D Land 1.0.0 as docs/FILLING.md fills it: one translation, a variant per region.
async function seedMario(published = 1) {
  await env.DB.batch([
    env.DB.prepare(
      `INSERT INTO translations (slug, title, kind, version, released_at, description, authors, text_pct, textures_pct,
                                 banner_sha256, published)
       VALUES ('super-mario-3d-land', 'Super Mario 3D Land', 'patch', '1.0.0', '2026-10-20', 'Перекладено всю гру.',
               'Side Quest', 100, 100, ?, ?)`,
    ).bind(SHA("b"), published),
    env.DB.prepare(
      `INSERT INTO variants (slug, region, title_id, product_code, file_key, file_size, file_sha256)
       VALUES ('super-mario-3d-land', 'EUR', '0004000000053F00', 'CTR-P-AREP',
               'super-mario-3d-land/patch/1.0.0/eur.zip', 69632, ?)`,
    ).bind(SHA("e")),
    env.DB.prepare(
      `INSERT INTO variants (slug, region, title_id, product_code, file_key, file_size, file_sha256)
       VALUES ('super-mario-3d-land', 'USA', '0004000000054000', 'CTR-P-AREE',
               'super-mario-3d-land/patch/1.0.0/usa.zip', 69632, ?)`,
    ).bind(SHA("a")),
  ]);
}

async function verify(body: string, signature: string): Promise<boolean> {
  const { alg: _, ...jwk } = JSON.parse(env.TEST_PUBLIC_KEY) as JsonWebKey;
  const key = await crypto.subtle.importKey("jwk", jwk, { name: "Ed25519" }, false, ["verify"]);
  const bytes = new Uint8Array(signature.match(/../g)!.map((h) => parseInt(h, 16)));
  return crypto.subtle.verify({ name: "Ed25519" }, key, bytes, new TextEncoder().encode(body));
}

describe("GET /v1/catalog", () => {
  beforeEach(async () => {
    await env.DB.exec("DELETE FROM screenshots; DELETE FROM variants; DELETE FROM translations; DELETE FROM events;");
  });

  it("lists a translation with a variant per region", async () => {
    await seedMario();

    const response = await SELF.fetch(URL);

    expect(response.status).toBe(200);
    const catalog = (await response.json()) as { v: number; items: any[] };
    expect(catalog.v).toBe(1);
    expect(catalog.items).toHaveLength(1);
    expect(catalog.items[0]).toMatchObject({
      slug: "super-mario-3d-land",
      kind: "patch",
      version: "1.0.0",
      text: 100,
      textures: 100,
      third_party: false,
      pinned: false,
      installs: 0,
      banner: { key: "super-mario-3d-land/banner.png", sha256: SHA("b") },
      icon: null,
      screenshots: [],
    });
    expect(catalog.items[0].variants).toEqual([
      { region: "EUR", tid: "0004000000053F00", product: "CTR-P-AREP", game_version: null,
        key: "super-mario-3d-land/patch/1.0.0/eur.zip", size: 69632, sha256: SHA("e") },
      { region: "USA", tid: "0004000000054000", product: "CTR-P-AREE", game_version: null,
        key: "super-mario-3d-land/patch/1.0.0/usa.zip", size: 69632, sha256: SHA("a") },
    ]);
  });

  it("signs the exact body with the catalog key", async () => {
    await seedMario();

    const response = await SELF.fetch(URL);
    const body = await response.text();

    expect(await verify(body, response.headers.get("X-Signature")!)).toBe(true);
    expect(await verify(body.replace("1.0.0", "9.9.9"), response.headers.get("X-Signature")!)).toBe(false);
  });

  it("leaves drafts out", async () => {
    await seedMario(0);

    const catalog = (await (await SELF.fetch(URL)).json()) as { items: unknown[] };

    expect(catalog.items).toEqual([]);
  });

  it("keeps unknown progress as null", async () => {
    await env.DB.prepare(
      `INSERT INTO translations (slug, title, kind, version, text_pct, textures_pct, third_party, published)
       VALUES ('some-cia-game', 'Some CIA Game', 'cia', '1.0', 100, NULL, 1, 1)`,
    ).run();

    const catalog = (await (await SELF.fetch(URL)).json()) as { items: any[] };

    expect(catalog.items[0]).toMatchObject({ text: 100, textures: null, third_party: true, variants: [] });
  });

  it("marks a pinned translation", async () => {
    await env.DB.prepare(
      `INSERT INTO translations (slug, title, kind, version, pinned, published) VALUES ('3ds-ua', 'Українізатор', 'cia', '2.1.0', 1, 1)`,
    ).run();

    const catalog = (await (await SELF.fetch(URL)).json()) as { items: any[] };

    expect(catalog.items[0]).toMatchObject({ slug: "3ds-ua", pinned: true });
  });

  it("counts consoles that installed a translation", async () => {
    await seedMario();
    await env.DB.prepare(
      `INSERT INTO events (install_id, event, app_version, slug) VALUES
         ('K7QM29XD', 'install', '1.0.0', 'super-mario-3d-land'),
         ('K7QM29XD', 'update',  '1.0.0', 'super-mario-3d-land'),
         ('AB12CD34', 'install', '1.0.0', 'super-mario-3d-land'),
         ('ZZ99ZZ99', 'error',   '1.0.0', 'super-mario-3d-land')`,
    ).run();

    const catalog = (await (await SELF.fetch(URL)).json()) as { items: any[] };

    expect(catalog.items[0].installs).toBe(2);
  });

  it("answers 304 to a console that already has this catalog", async () => {
    await seedMario();
    const first = await SELF.fetch(URL);
    await first.text();

    const again = await SELF.fetch(URL, { headers: { "If-None-Match": first.headers.get("ETag")! } });

    expect(again.status).toBe(304);
  });

  it("carries the SD data of a CIA that needs it, and no data field otherwise", async () => {
    await seedMario();
    await env.DB.batch([
      env.DB.prepare(
        `INSERT INTO translations (slug, title, kind, version, published) VALUES ('undertale', 'Undertale', 'cia', '1.0', 1)`,
      ),
      env.DB.prepare(
        `INSERT INTO variants (slug, region, title_id, file_key, file_size, file_sha256,
                               data_key, data_size, data_sha256, data_dir)
         VALUES ('undertale', 'EUR', '0004000000EFAD00', 'undertale/cia/1.0/eur.cia', 2280896, ?,
                 'undertale/data/1.0/eur.zip', 250000000, ?, '3ds/butterscotch/undertale')`,
      ).bind(SHA("c"), SHA("d")),
    ]);

    const catalog = (await (await SELF.fetch(URL)).json()) as { items: any[] };

    const undertale = catalog.items.find((i) => i.slug === "undertale");
    expect(undertale.variants[0].data).toEqual({
      key: "undertale/data/1.0/eur.zip", size: 250000000, sha256: SHA("d"), dir: "3ds/butterscotch/undertale",
    });
    const mario = catalog.items.find((i) => i.slug === "super-mario-3d-land");
    expect(mario.variants[0]).not.toHaveProperty("data");
  });

  it("refuses a data folder outside 3ds/ or without its file", async () => {
    await env.DB.prepare(
      `INSERT INTO translations (slug, title, kind, version) VALUES ('undertale', 'Undertale', 'cia', '1.0')`,
    ).run();
    const variant = `INSERT INTO variants (slug, region, title_id, file_key, file_size, file_sha256,
                                           data_key, data_size, data_sha256, data_dir) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`;
    const row = (key: string | null, dir: string) =>
      env.DB.prepare(variant)
        .bind("undertale", "EUR", "0004000000EFAD00", `undertale/cia/1.0/${dir.length}.cia`, 1, SHA("c"), key, 1, SHA("d"), dir)
        .run();
    for (const dir of ["luma/titles/x", "3ds/../luma", "3ds/x/", "/3ds/x", "3ds/3ds-ua-hub/backup", "3ds//x"])
      await expect(row("undertale/data/1.0/eur.zip", dir), dir).rejects.toThrow(/CHECK constraint failed/);
    await expect(row(null, "3ds/butterscotch/undertale")).rejects.toThrow(/CHECK constraint failed/);
  });

  it("lists the screenshots in order, with their captions", async () => {
    await seedMario();
    await env.DB.batch([
      env.DB.prepare(`INSERT INTO screenshots (slug, position, sha256, caption) VALUES ('super-mario-3d-land', 2, ?, NULL)`)
        .bind(SHA("2")),
      env.DB.prepare(`INSERT INTO screenshots (slug, position, sha256, caption) VALUES ('super-mario-3d-land', 1, ?, ?)`)
        .bind(SHA("1"), "Вибір файлу: «НОВА ГРА»"),
    ]);

    const catalog = (await (await SELF.fetch(URL)).json()) as { items: any[] };

    expect(catalog.items[0].screenshots).toEqual([
      { key: "super-mario-3d-land/shots/1.png", sha256: SHA("1"), caption: "Вибір файлу: «НОВА ГРА»" },
      { key: "super-mario-3d-land/shots/2.png", sha256: SHA("2"), caption: null },
    ]);
  });

  it("refuses a screenshot past the sixth or with a bad hash", async () => {
    await seedMario();
    const shot = `INSERT INTO screenshots (slug, position, sha256) VALUES (?, ?, ?)`;
    for (const [position, sha] of [[0, SHA("1")], [7, SHA("1")], [1, SHA("A")], [1, "abc"]] as const)
      await expect(env.DB.prepare(shot).bind("super-mario-3d-land", position, sha).run(), `${position} ${sha}`)
        .rejects.toThrow(/CHECK constraint failed/);
    await expect(env.DB.prepare(shot).bind("no-such-game", 1, SHA("1")).run()).rejects.toThrow(/FOREIGN KEY/);
  });

  it("refuses rows a typo would break", async () => {
    const bad = [
      `INSERT INTO translations (slug, title, kind, version) VALUES ('Super Mario', 't', 'patch', '1')`,
      `INSERT INTO translations (slug, title, kind, version) VALUES ('x', 't', 'zip', '1')`,
      `INSERT INTO translations (slug, title, kind, version, text_pct) VALUES ('x', 't', 'patch', '1', 120)`,
    ];
    for (const sql of bad) await expect(env.DB.prepare(sql).run(), sql).rejects.toThrow(/CHECK constraint failed/);

    await seedMario();
    const variant = `INSERT INTO variants (slug, region, title_id, file_key, file_size, file_sha256) VALUES (?, ?, ?, ?, ?, ?)`;
    await expect(
      env.DB.prepare(variant).bind("super-mario-3d-land", "EUR", "0004000000053f00", "k", 1, SHA("e")).run(),
    ).rejects.toThrow(/CHECK constraint failed/);
    await expect(
      env.DB.prepare(variant).bind("no-such-game", "EUR", "0004000000053F00", "k", 1, SHA("e")).run(),
    ).rejects.toThrow(/FOREIGN KEY/);
  });
});
