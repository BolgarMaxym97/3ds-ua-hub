-- Screenshots of a translation in the game: up to 6 per translation, shown in order on its page (R).
-- Each is one PNG of both screens as the console shows them: the top 400x240 over the bottom
-- 320x240, centred, 400x480 in all. The file lives at <slug>/shots/<position>.png.
CREATE TABLE screenshots (
  slug     TEXT    NOT NULL REFERENCES translations (slug) ON DELETE CASCADE,
  position INTEGER NOT NULL CHECK (position BETWEEN 1 AND 6),
  sha256   TEXT    NOT NULL CHECK (length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'),
  caption  TEXT,   -- what the screenshot shows, "Вибір файлу: «НОВА ГРА»"; NULL: none
  PRIMARY KEY (slug, position)
);
