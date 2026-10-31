-- A patch for a game the person does not have: a link to a CIA of that game, which the app downloads
-- and installs before the patch (D4 "Завантажити гру"). The CIA lives on someone else's server;
-- only the link is kept here, and the console gets the bytes through GET /v1/fallback/<slug>/<TID>.
-- The size and sha256 are the file's as it was when the link was added: the app refuses another one.
ALTER TABLE variants ADD COLUMN fallback_url TEXT
  CHECK (fallback_url GLOB 'http://?*' OR fallback_url GLOB 'https://?*');
ALTER TABLE variants ADD COLUMN fallback_size INTEGER CHECK (fallback_size > 0);
ALTER TABLE variants ADD COLUMN fallback_sha256 TEXT
  CHECK (length(fallback_sha256) = 64 AND fallback_sha256 NOT GLOB '*[^0-9a-f]*');

-- All three or none, and never for a patch that needs a game update: the CIA brings the game alone.
-- (ALTER TABLE cannot add a CHECK across columns.)
CREATE TRIGGER variants_fallback_insert BEFORE INSERT ON variants
WHEN NOT ((NEW.fallback_url IS NULL AND NEW.fallback_size IS NULL AND NEW.fallback_sha256 IS NULL)
          OR (NEW.fallback_url IS NOT NULL AND NEW.fallback_size IS NOT NULL AND NEW.fallback_sha256 IS NOT NULL
              AND NEW.game_version IS NULL))
BEGIN
  SELECT RAISE(ABORT, 'fallback_url, fallback_size and fallback_sha256 go together, and not with game_version');
END;

CREATE TRIGGER variants_fallback_update BEFORE UPDATE ON variants
WHEN NOT ((NEW.fallback_url IS NULL AND NEW.fallback_size IS NULL AND NEW.fallback_sha256 IS NULL)
          OR (NEW.fallback_url IS NOT NULL AND NEW.fallback_size IS NOT NULL AND NEW.fallback_sha256 IS NOT NULL
              AND NEW.game_version IS NULL))
BEGIN
  SELECT RAISE(ABORT, 'fallback_url, fallback_size and fallback_sha256 go together, and not with game_version');
END;
