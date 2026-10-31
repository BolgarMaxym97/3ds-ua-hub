// GET|HEAD /v1/fallback/<slug>/<TID> - the game a patch is for, for whoever does not have it.
// The CIA lives on someone else's server (variants.fallback_url); the console cannot reach most of
// them (its TLS is too old for today's HTTPS), so the bytes come through here, with Range for
// resuming, and nothing is kept. Only links of the catalog are followed: no open proxy.
//
// HEAD checks the link before the app offers the download (J4): the server answers, with a file
// of the catalog's size. Anything wrong is a 502 whose X-Fallback-Error says what, and the app
// tells the person (J7). The app checks the sha256 itself, as it arrives.

export type Upstream = (url: string, init: RequestInit) => Promise<Response>;

const PATH = /^\/v1\/fallback\/([a-z0-9][a-z0-9-]*)\/([0-9A-F]{16})$/;
const TIMEOUT_MS = 15_000;

export function fallbackTarget(pathname: string): { slug: string; tid: string } | null {
  const match = PATH.exec(pathname);
  return match ? { slug: match[1], tid: match[2] } : null;
}

export async function serveFallback(
  request: Request,
  db: D1Database,
  slug: string,
  tid: string,
  upstream: Upstream = (url, init) => fetch(url, init),
): Promise<Response> {
  const link = await db
    .prepare(
      `SELECT v.fallback_url AS url, v.fallback_size AS size
       FROM variants v JOIN translations t ON t.slug = v.slug
       WHERE v.slug = ? AND v.title_id = ? AND t.published = 1 AND t.kind = 'patch' AND v.fallback_url IS NOT NULL`,
    )
    .bind(slug, tid)
    .first<{ url: string; size: number }>();
  if (!link) return new Response(null, { status: 404 });

  // A HEAD to the server would be cheaper, but many answer it differently from a GET (or not at
  // all); the first byte of a GET says the same about the file.
  const head = request.method === "HEAD";
  const range = head ? "bytes=0-0" : request.headers.get("Range");
  let response: Response;
  try {
    response = await upstream(link.url, {
      headers: range ? { Range: range } : {},
      redirect: "follow",
      signal: AbortSignal.timeout(TIMEOUT_MS),
    });
  } catch {
    return broken("unreachable");
  }

  if (response.status === 416 && !head) {
    await response.body?.cancel();
    return new Response(null, { status: 416, headers: { "Content-Range": `bytes */${link.size}` } });
  }
  const problem = check(response, link.size);
  if (problem) {
    await response.body?.cancel();
    return broken(problem);
  }

  const headers = new Headers({
    "Content-Type": "application/octet-stream",
    "Accept-Ranges": "bytes",
    "Cache-Control": "no-store",
  });
  if (head) {
    await response.body?.cancel();
    headers.set("Content-Length", String(link.size));
    return new Response(null, { headers });
  }
  // A server that ignored the Range sends the whole file with 200: so does this, and the app
  // starts its .part over.
  const length = response.headers.get("Content-Length");
  if (length !== null) headers.set("Content-Length", length);
  const span = response.headers.get("Content-Range");
  if (response.status === 206 && span !== null) headers.set("Content-Range", span);
  return new Response(response.body, { status: response.status, headers });
}

// What is wrong with the server's answer for a file of `size` bytes, or null.
function check(response: Response, size: number): string | null {
  if (response.status !== 200 && response.status !== 206) return `status-${response.status}`;
  // a "file not found" or a captcha page sent with 200
  if ((response.headers.get("Content-Type") ?? "").toLowerCase().includes("text/html")) return "html";
  const total = totalSize(response);
  if (total !== null && total !== size) return `size-${total}`;
  return null;
}

// The whole file's size: from "bytes 0-0/1234" of a 206, or Content-Length of a 200.
function totalSize(response: Response): number | null {
  if (response.status === 206) {
    const match = /\/(\d+)\s*$/.exec(response.headers.get("Content-Range") ?? "");
    return match ? Number(match[1]) : null;
  }
  const length = response.headers.get("Content-Length");
  return length === null ? null : Number(length);
}

function broken(reason: string): Response {
  return new Response(reason, {
    status: 502,
    headers: { "X-Fallback-Error": reason, "Content-Type": "text/plain", "Cache-Control": "no-store" },
  });
}
