import { buildCatalog, sha256, sign } from "./catalog";
import { MAX_BODY, parseEvent, type StatsEvent } from "./event";
import { fallbackTarget, serveFallback } from "./fallback";
import { serveFile } from "./files";

// GET      /v1/catalog  - published translations, signed (X-Signature) - catalog.ts
// GET|HEAD /v1/f/<key>  - a file from R2, with Range - files.ts
// GET|HEAD /v1/fallback/<slug>/<TID> - a patch's game from the server its link names - fallback.ts
// POST     /v1/event    - one stats report from the app; 204, the app does not read the answer
// GET      /v1/health   - "ok", for checking a console can reach the Worker at all
export default {
  async fetch(request, env): Promise<Response> {
    const { pathname } = new URL(request.url);

    if (pathname === "/v1/health") return only(request, ["GET"]) ?? new Response("ok");
    if (pathname === "/v1/catalog") return only(request, ["GET"]) ?? catalog(request, env);
    if (pathname.startsWith("/v1/f/")) {
      return only(request, ["GET", "HEAD"]) ?? serveFile(request, env.FILES, decodeURIComponent(pathname.slice(6)));
    }
    const fallback = fallbackTarget(pathname);
    if (fallback) {
      return only(request, ["GET", "HEAD"]) ?? serveFallback(request, env.DB, fallback.slug, fallback.tid);
    }
    if (pathname === "/v1/event") return only(request, ["POST"]) ?? event(request, env);
    return new Response(null, { status: 404 });
  },
} satisfies ExportedHandler<Env>;

function only(request: Request, methods: string[]): Response | null {
  if (methods.includes(request.method)) return null;
  return new Response(null, { status: 405, headers: { Allow: methods.join(", ") } });
}

async function catalog(request: Request, env: Env): Promise<Response> {
  const body = await buildCatalog(env.DB);
  const etag = `"${await sha256(body)}"`;
  if (request.headers.get("If-None-Match") === etag) return new Response(null, { status: 304, headers: { ETag: etag } });

  return new Response(body, {
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "X-Signature": await sign(body, env.SIGNING_KEY),
      ETag: etag,
      // The catalog changes whenever a row is edited by hand; let consoles see that at once.
      "Cache-Control": "no-cache",
    },
  });
}

async function event(request: Request, env: Env): Promise<Response> {
  const declared = Number(request.headers.get("Content-Length") ?? 0);
  if (declared > MAX_BODY) return new Response(null, { status: 413 });
  const body = await request.text();
  if (body.length > MAX_BODY) return new Response(null, { status: 413 });

  let raw: unknown;
  try {
    raw = JSON.parse(body);
  } catch {
    return new Response("bad json", { status: 400 });
  }
  const parsed = parseEvent(raw);
  if (!parsed.ok) return new Response(parsed.error, { status: 400 });

  await insert(env.DB, parsed.event);
  return new Response(null, { status: 204 });
}

const COLUMNS = [
  "install_id", "event", "app_version", "model", "system_version", "luma_version", "region", "slug",
  "translation_version", "variant_region", "title_id", "kind", "manual", "risky", "duration_ms",
  "error_stage", "error_code", "error_detail", "log",
] as const satisfies readonly (keyof StatsEvent)[];

const INSERT = `INSERT INTO events (${COLUMNS.join(", ")}) VALUES (${COLUMNS.map(() => "?").join(", ")})`;

async function insert(db: D1Database, event: StatsEvent): Promise<void> {
  await db.prepare(INSERT).bind(...COLUMNS.map((column) => event[column])).run();
}
