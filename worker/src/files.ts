// GET|HEAD /v1/f/<key> - a file from R2, with Range so a console can resume a broken download.
// The app checks the sha256 from the signed catalog after the last byte, so this route only has
// to hand bytes out; it serves the key shapes docs/FILLING.md describes and nothing else.

const KEY = /^[a-z0-9][a-z0-9-]*\/(?:(?:patch|data)\/[0-9A-Za-z.-]+\/[a-z]+\.zip|cia\/[0-9A-Za-z.-]+\/[a-z]+\.cia|(?:banner|icon)\.png|shots\/[1-6]\.png)$/;

export function isFileKey(key: string): boolean {
  return KEY.test(key) && !key.includes("..");
}

export async function serveFile(request: Request, bucket: R2Bucket, key: string): Promise<Response> {
  if (!isFileKey(key)) return new Response(null, { status: 404 });

  if (request.method === "HEAD") {
    const object = await bucket.head(key);
    if (!object) return new Response(null, { status: 404 });
    return new Response(null, { headers: headersFor(object, object.size) });
  }

  const range = request.headers.get("Range");
  if (range === null) {
    const object = await bucket.get(key);
    if (!object) return new Response(null, { status: 404 });
    return new Response(object.body, { headers: headersFor(object, object.size) });
  }

  // The range is checked here, not left to R2: R2 answers a range past the end with the whole
  // file, and a console resuming into its .part would append all of it.
  const head = await bucket.head(key);
  if (!head) return new Response(null, { status: 404 });
  const span = parseRange(range, head.size);
  if (span === "unsatisfiable") {
    return new Response(null, { status: 416, headers: { "Content-Range": `bytes */${head.size}` } });
  }
  const object = await bucket.get(key, span === null ? {} : { range: span });
  if (!object) return new Response(null, { status: 404 });
  if (span === null) return new Response(object.body, { headers: headersFor(object, object.size) });

  const headers = headersFor(object, span.length);
  headers.set("Content-Range", `bytes ${span.offset}-${span.offset + span.length - 1}/${object.size}`);
  return new Response(object.body, { status: 206, headers });
}

// One range of bytes: "bytes=N-", "bytes=N-M" or "bytes=-N". Anything else (several ranges, other
// units) is ignored and the whole file goes out, which HTTP allows.
export function parseRange(header: string, size: number): { offset: number; length: number } | "unsatisfiable" | null {
  const match = /^bytes=(\d*)-(\d*)$/.exec(header.trim());
  if (!match || (match[1] === "" && match[2] === "")) return null;
  if (match[1] === "") {
    const length = Math.min(Number(match[2]), size);
    return length === 0 ? "unsatisfiable" : { offset: size - length, length };
  }
  const offset = Number(match[1]);
  const end = match[2] === "" ? size - 1 : Math.min(Number(match[2]), size - 1);
  if (offset >= size || end < offset) return "unsatisfiable";
  return { offset, length: end - offset + 1 };
}

function headersFor(object: R2Object, length: number): Headers {
  const headers = new Headers();
  headers.set("Content-Type", contentType(object.key));
  headers.set("Content-Length", String(length));
  headers.set("Accept-Ranges", "bytes");
  headers.set("ETag", object.httpEtag);
  // Patch and CIA keys carry the version, so their bytes never change; a banner or an icon is
  // replaced in place, and the app checks it against the catalog's sha256 anyway.
  const versioned = /\/(?:patch|cia)\//.test(object.key);
  headers.set("Cache-Control", versioned ? "public, max-age=31536000, immutable" : "no-cache");
  return headers;
}

function contentType(key: string): string {
  if (key.endsWith(".png")) return "image/png";
  if (key.endsWith(".zip")) return "application/zip";
  return "application/octet-stream";
}
