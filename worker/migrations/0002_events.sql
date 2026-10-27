-- One row per report from the app. No IP, no country: the Worker never reads them.
CREATE TABLE events (
  id                  INTEGER PRIMARY KEY AUTOINCREMENT,
  received_at         TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
  install_id          TEXT    NOT NULL,  -- random, made on the console; shown on the Help screen
  event               TEXT    NOT NULL,  -- install | update | remove | error | not_found
  app_version         TEXT    NOT NULL,
  model               TEXT,
  system_version      TEXT,
  luma_version        TEXT,
  region              TEXT,              -- of the console
  slug                TEXT,
  translation_version TEXT,
  variant_region      TEXT,
  title_id            TEXT,              -- the game it went to; for not_found, the one picked by hand
  kind                TEXT,              -- patch | cia
  manual              INTEGER,           -- 1: game picked by hand
  risky               INTEGER,           -- 1: installed although the files differ
  duration_ms         INTEGER,
  error_stage         TEXT,
  error_code          TEXT,              -- GAME_VERSION, SD_WRITE, ... (Errors board)
  error_detail        TEXT,
  log                 TEXT
);

CREATE INDEX events_install ON events (install_id, received_at);
CREATE INDEX events_received ON events (received_at);
CREATE INDEX events_slug ON events (slug, event);
