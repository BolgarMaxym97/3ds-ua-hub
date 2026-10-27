// The report the app sends - the one place its shape is written down on this side.
// app/source/stats.c builds it; keep the two in step.

export const MAX_BODY = 4096;

// Crockford base32 without the dash the Help screen shows: no I, L, O, U.
const INSTALL_ID = /^[0-9A-HJKMNP-TV-Z]{8}$/;
const EVENTS = ["install", "update", "remove", "error", "not_found"] as const;
const KINDS = ["patch", "cia"] as const;
const TITLE_ID = /^[0-9A-F]{16}$/;

export interface StatsEvent {
  install_id: string;
  event: (typeof EVENTS)[number];
  app_version: string;
  model: string | null;
  system_version: string | null;
  luma_version: string | null;
  region: string | null;
  slug: string | null;
  translation_version: string | null;
  variant_region: string | null;
  title_id: string | null;
  kind: string | null;
  manual: number | null;
  risky: number | null;
  duration_ms: number | null;
  error_stage: string | null;
  error_code: string | null;
  error_detail: string | null;
  log: string | null;
}

type Result = { ok: true; event: StatsEvent } | { ok: false; error: string };

// Optional short text: absent or null -> null; anything else must be a string within `max`.
function text(body: Record<string, unknown>, key: string, max: number): string | null | undefined {
  const value = body[key];
  if (value === undefined || value === null) return null;
  if (typeof value !== "string" || value.length > max) return undefined;
  return value;
}

function integer(body: Record<string, unknown>, key: string): number | null | undefined {
  const value = body[key];
  if (value === undefined || value === null) return null;
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value < -(2 ** 31) || value > 2 ** 31) return undefined;
  return value;
}

// true/false -> 1/0, so SQL can sum it.
function flag(body: Record<string, unknown>, key: string): number | null | undefined {
  const value = body[key];
  if (value === undefined || value === null) return null;
  if (typeof value !== "boolean") return undefined;
  return value ? 1 : 0;
}

function oneOf(body: Record<string, unknown>, key: string, allowed: readonly string[]): string | null | undefined {
  const value = text(body, key, 16);
  if (value === null || value === undefined) return value;
  return allowed.includes(value) ? value : undefined;
}

export function parseEvent(raw: unknown): Result {
  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) return { ok: false, error: "not an object" };
  const body = raw as Record<string, unknown>;

  if (body.v !== 1) return { ok: false, error: "unknown version" };
  if (typeof body.id !== "string" || !INSTALL_ID.test(body.id)) return { ok: false, error: "bad id" };
  if (typeof body.event !== "string" || !(EVENTS as readonly string[]).includes(body.event)) {
    return { ok: false, error: "bad event" };
  }
  const app = text(body, "app", 16);
  if (!app) return { ok: false, error: "bad app" };

  const tid = text(body, "tid", 16);
  const fields = {
    model: text(body, "model", 32),
    system_version: text(body, "system", 32),
    luma_version: text(body, "luma", 16),
    region: text(body, "region", 8),
    slug: text(body, "slug", 64),
    translation_version: text(body, "tv", 16),
    variant_region: text(body, "vregion", 8),
    title_id: tid === null || (tid !== undefined && TITLE_ID.test(tid)) ? tid : undefined,
    kind: oneOf(body, "kind", KINDS),
    manual: flag(body, "manual"),
    risky: flag(body, "risky"),
    duration_ms: integer(body, "ms"),
    error_stage: text(body, "stage", 32),
    error_code: text(body, "code", 32),
    error_detail: text(body, "detail", 256),
    log: text(body, "log", 2048),
  };
  for (const [key, value] of Object.entries(fields)) {
    if (value === undefined) return { ok: false, error: `bad ${key}` };
  }

  return {
    ok: true,
    event: { install_id: body.id, event: body.event as StatsEvent["event"], app_version: app, ...fields } as StatsEvent,
  };
}
