// GET /v1/catalog - every published translation with its variants, as the app reads it.
// The app talks plain HTTP (the 3DS's own TLS cannot reach workers.dev), so the body is signed
// with Ed25519 and carries the sha256 of every file; the app checks both before using anything.
// app/source/catalog.c reads this; keep the two in step.

export const CATALOG_VERSION = 1;

interface TranslationRow {
  slug: string;
  title: string;
  kind: "patch" | "cia";
  version: string;
  released_at: string | null;
  description: string | null;
  changelog: string | null;
  authors: string | null;
  authors_url: string | null;
  text_pct: number | null;
  textures_pct: number | null;
  third_party: number;
  pinned: number;
  min_app: string | null;
  banner_sha256: string | null;
  icon_sha256: string | null;
  updated_at: string;
}

interface VariantRow {
  slug: string;
  region: string;
  title_id: string;
  product_code: string | null;
  game_version: number | null;
  file_key: string;
  file_size: number;
  file_sha256: string;
  data_key: string | null;
  data_size: number | null;
  data_sha256: string | null;
  data_dir: string | null;
}

export async function buildCatalog(db: D1Database): Promise<string> {
  const [translations, variants, installs] = await db.batch([
    db.prepare(
      `SELECT slug, title, kind, version, released_at, description, changelog, authors, authors_url,
              text_pct, textures_pct, third_party, pinned, min_app, banner_sha256, icon_sha256, updated_at
       FROM translations WHERE published = 1 ORDER BY updated_at DESC, slug`,
    ),
    db.prepare(
      `SELECT v.slug, v.region, v.title_id, v.product_code, v.game_version, v.file_key, v.file_size, v.file_sha256,
              v.data_key, v.data_size, v.data_sha256, v.data_dir
       FROM variants v JOIN translations t ON t.slug = v.slug
       WHERE t.published = 1 ORDER BY v.slug, v.region, v.title_id`,
    ),
    // Popularity for the "Популярні" sort: consoles that installed it at least once.
    db.prepare(
      `SELECT slug, COUNT(DISTINCT install_id) AS n FROM events
       WHERE event IN ('install', 'update') AND slug IS NOT NULL GROUP BY slug`,
    ),
  ]);

  const bySlug = new Map<string, VariantRow[]>();
  for (const v of variants.results as unknown as VariantRow[]) {
    const list = bySlug.get(v.slug) ?? [];
    list.push(v);
    bySlug.set(v.slug, list);
  }
  const counts = new Map((installs.results as { slug: string; n: number }[]).map((r) => [r.slug, r.n]));

  const items = (translations.results as unknown as TranslationRow[]).map((t) => ({
    slug: t.slug,
    title: t.title,
    kind: t.kind,
    version: t.version,
    released: t.released_at,
    updated: t.updated_at,
    description: t.description,
    changelog: t.changelog,
    authors: t.authors,
    authors_url: t.authors_url,
    text: t.text_pct,
    textures: t.textures_pct,
    third_party: t.third_party === 1,
    pinned: t.pinned === 1,
    min_app: t.min_app,
    installs: counts.get(t.slug) ?? 0,
    banner: t.banner_sha256 === null ? null : { key: `${t.slug}/banner.png`, sha256: t.banner_sha256 },
    icon: t.icon_sha256 === null ? null : { key: `${t.slug}/icon.png`, sha256: t.icon_sha256 },
    variants: (bySlug.get(t.slug) ?? []).map((v) => ({
      region: v.region,
      tid: v.title_id,
      product: v.product_code,
      game_version: v.game_version,
      key: v.file_key,
      size: v.file_size,
      sha256: v.file_sha256,
      // a CIA whose game lives on the SD card: the zip unpacked into <dir>
      ...(v.data_dir === null ? {} : { data: { key: v.data_key, size: v.data_size, sha256: v.data_sha256, dir: v.data_dir } }),
    })),
  }));

  return JSON.stringify({ v: CATALOG_VERSION, items });
}

// The secret holds the private key as a JWK (scripts/keygen.mjs makes it).
export async function sign(body: string, jwk: string): Promise<string> {
  // `alg` is optional and spelled "EdDSA" or "Ed25519" depending on who exported the key;
  // workerd refuses the one it does not expect, and `crv` already says Ed25519.
  const { alg: _, ...privateJwk } = JSON.parse(jwk) as JsonWebKey;
  const key = await crypto.subtle.importKey("jwk", privateJwk, { name: "Ed25519" }, false, ["sign"]);
  const signature = await crypto.subtle.sign({ name: "Ed25519" }, key, new TextEncoder().encode(body));
  return hex(new Uint8Array(signature));
}

export async function sha256(body: string): Promise<string> {
  return hex(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(body))));
}

function hex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}
