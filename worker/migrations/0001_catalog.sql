-- The catalog, filled by hand (docs/FILLING.md). The CHECKs catch typos before a console sees them.

-- One row per translation, shown in the app as one entry.
CREATE TABLE translations (
  slug          TEXT    PRIMARY KEY CHECK (slug GLOB '[a-z0-9]*' AND slug NOT GLOB '*[^a-z0-9-]*'),
  title         TEXT    NOT NULL,
  kind          TEXT    NOT NULL CHECK (kind IN ('patch', 'cia')),
  version       TEXT    NOT NULL,                -- of the translation, "1.0.0"
  released_at   TEXT,                            -- "2026-10-25"
  description   TEXT,
  changelog     TEXT,                            -- what changed in this version
  authors       TEXT,
  authors_url   TEXT,
  text_pct      INTEGER CHECK (text_pct BETWEEN 0 AND 100),      -- NULL: unknown
  textures_pct  INTEGER CHECK (textures_pct BETWEEN 0 AND 100),  -- NULL: unknown
  third_party   INTEGER NOT NULL DEFAULT 0 CHECK (third_party IN (0, 1)),
  min_app       TEXT,                            -- oldest Hub that can install it; NULL: any
  banner_sha256 TEXT    CHECK (length(banner_sha256) = 64),  -- <slug>/banner.png; NULL: no banner
  icon_sha256   TEXT    CHECK (length(icon_sha256) = 64),    -- <slug>/icon.png;   NULL: no icon
  published     INTEGER NOT NULL DEFAULT 0 CHECK (published IN (0, 1)),  -- 0: draft, not in the catalog
  updated_at    TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

-- One row per region (or per game build) of a translation. The app picks the one that fits the console.
CREATE TABLE variants (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  slug          TEXT    NOT NULL REFERENCES translations (slug) ON DELETE CASCADE,
  region        TEXT    NOT NULL CHECK (region IN ('EUR', 'USA', 'JPN', 'KOR', 'CHN', 'TWN')),
  title_id      TEXT    NOT NULL CHECK (length(title_id) = 16 AND title_id NOT GLOB '*[^0-9A-F]*'),
  product_code  TEXT,                            -- "CTR-P-AREP", for picking a game by hand
  game_version  INTEGER CHECK (game_version >= 0),  -- update title version it needs; NULL: any
  file_key      TEXT    NOT NULL UNIQUE,         -- <slug>/patch/<version>/<region>.zip or <slug>/cia/...
  file_size     INTEGER NOT NULL CHECK (file_size > 0),
  file_sha256   TEXT    NOT NULL CHECK (length(file_sha256) = 64 AND file_sha256 NOT GLOB '*[^0-9a-f]*'),
  UNIQUE (slug, region, title_id)
);

CREATE INDEX variants_slug ON variants (slug);
