-- 0006 kept fallback_url away from every variant with a game_version. Too strict for 0: a patch
-- for the game without updates fits exactly what a game CIA brings. Still refused above 0: the
-- CIA brings no update, and the patch would not fit it.
DROP TRIGGER variants_fallback_insert;
DROP TRIGGER variants_fallback_update;

CREATE TRIGGER variants_fallback_insert BEFORE INSERT ON variants
WHEN NOT ((NEW.fallback_url IS NULL AND NEW.fallback_size IS NULL AND NEW.fallback_sha256 IS NULL)
          OR (NEW.fallback_url IS NOT NULL AND NEW.fallback_size IS NOT NULL AND NEW.fallback_sha256 IS NOT NULL
              AND (NEW.game_version IS NULL OR NEW.game_version = 0)))
BEGIN
  SELECT RAISE(ABORT, 'fallback_url, fallback_size and fallback_sha256 go together, and not with a game_version above 0');
END;

CREATE TRIGGER variants_fallback_update BEFORE UPDATE ON variants
WHEN NOT ((NEW.fallback_url IS NULL AND NEW.fallback_size IS NULL AND NEW.fallback_sha256 IS NULL)
          OR (NEW.fallback_url IS NOT NULL AND NEW.fallback_size IS NOT NULL AND NEW.fallback_sha256 IS NOT NULL
              AND (NEW.game_version IS NULL OR NEW.game_version = 0)))
BEGIN
  SELECT RAISE(ABORT, 'fallback_url, fallback_size and fallback_sha256 go together, and not with a game_version above 0');
END;
