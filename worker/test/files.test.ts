import { SELF } from "cloudflare:test";
import { env } from "cloudflare:workers";
import { beforeAll, describe, expect, it } from "vitest";

const KEY = "super-mario-3d-land/patch/1.0.0/eur.zip";
const URL = `https://hub.example/v1/f/${KEY}`;
const BYTES = new Uint8Array(1000).map((_, i) => i % 251);

describe("GET /v1/f/<key>", () => {
  beforeAll(async () => {
    await env.FILES.put(KEY, BYTES);
    await env.FILES.put("super-mario-3d-land/banner.png", new Uint8Array([0x89, 0x50, 0x4e, 0x47]));
  });

  it("hands out the whole file", async () => {
    const response = await SELF.fetch(URL);

    expect(response.status).toBe(200);
    expect(response.headers.get("Content-Length")).toBe("1000");
    expect(response.headers.get("Accept-Ranges")).toBe("bytes");
    expect(response.headers.get("Cache-Control")).toContain("immutable");
    expect(new Uint8Array(await response.arrayBuffer())).toEqual(BYTES);
  });

  it("resumes from a byte offset", async () => {
    const response = await SELF.fetch(URL, { headers: { Range: "bytes=400-" } });

    expect(response.status).toBe(206);
    expect(response.headers.get("Content-Range")).toBe("bytes 400-999/1000");
    expect(response.headers.get("Content-Length")).toBe("600");
    expect(new Uint8Array(await response.arrayBuffer())).toEqual(BYTES.slice(400));
  });

  it("serves a closed range and a suffix", async () => {
    const closed = await SELF.fetch(URL, { headers: { Range: "bytes=10-19" } });
    expect(closed.status).toBe(206);
    expect(closed.headers.get("Content-Range")).toBe("bytes 10-19/1000");
    expect(new Uint8Array(await closed.arrayBuffer())).toEqual(BYTES.slice(10, 20));

    const suffix = await SELF.fetch(URL, { headers: { Range: "bytes=-100" } });
    expect(suffix.headers.get("Content-Range")).toBe("bytes 900-999/1000");
    expect(new Uint8Array(await suffix.arrayBuffer())).toEqual(BYTES.slice(900));
  });

  it("says 416 when the console already has every byte", async () => {
    for (const range of ["bytes=1000-", "bytes=5000-"]) {
      const response = await SELF.fetch(URL, { headers: { Range: range } });
      expect(response.status, range).toBe(416);
      expect(response.headers.get("Content-Range")).toBe("bytes */1000");
    }
  });

  it("answers HEAD with the size", async () => {
    const response = await SELF.fetch(URL, { method: "HEAD" });

    expect(response.status).toBe(200);
    expect(response.headers.get("Content-Length")).toBe("1000");
  });

  it("serves banners as PNG", async () => {
    const response = await SELF.fetch("https://hub.example/v1/f/super-mario-3d-land/banner.png");

    expect(response.status).toBe(200);
    expect(response.headers.get("Content-Type")).toBe("image/png");
    // Replaced in place when a new banner is uploaded, unlike versioned archives.
    expect(response.headers.get("Cache-Control")).toBe("no-cache");
    await response.arrayBuffer();
  });

  it("knows only the documented key shapes", async () => {
    for (const key of ["super-mario-3d-land/notes.txt", "../secret", "Super/patch/1/eur.zip", "x/patch/1/../../y.zip",
                       "x/cia/1/eur.zip", "x/data/1/eur.cia", "x/patch/1/eur.cia"]) {
      const response = await SELF.fetch(`https://hub.example/v1/f/${key}`);
      expect(response.status, key).toBe(404);
    }
  });

  it("serves the data of a CIA that keeps its game on the SD card", async () => {
    await env.FILES.put("undertale/data/1.0/eur.zip", "game data");

    const response = await SELF.fetch("https://hub.example/v1/f/undertale/data/1.0/eur.zip");

    expect(response.status).toBe(200);
    expect(await response.text()).toBe("game data");
  });

  it("says 404 for a missing file", async () => {
    const response = await SELF.fetch("https://hub.example/v1/f/super-mario-3d-land/patch/9.9.9/eur.zip");

    expect(response.status).toBe(404);
  });

  it("does not write", async () => {
    const response = await SELF.fetch(URL, { method: "PUT", body: "x" });

    expect(response.status).toBe(405);
  });
});
