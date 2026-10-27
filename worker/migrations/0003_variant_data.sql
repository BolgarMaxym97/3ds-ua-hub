-- A CIA that reads its game from the SD card (a port such as Undertale's): besides the CIA, a zip
-- the app unpacks into <data_dir> on the card. data_dir is set only with the other three; the
-- catalog carries the data when data_dir is set.
ALTER TABLE variants ADD COLUMN data_key TEXT;  -- <slug>/data/<version>/<region>.zip
ALTER TABLE variants ADD COLUMN data_size INTEGER CHECK (data_size > 0);
ALTER TABLE variants ADD COLUMN data_sha256 TEXT
  CHECK (length(data_sha256) = 64 AND data_sha256 NOT GLOB '*[^0-9a-f]*');
-- relative to the SD root, under 3ds/ but not the Hub's own folder: "3ds/butterscotch/undertale"
ALTER TABLE variants ADD COLUMN data_dir TEXT
  CHECK (data_dir IS NULL OR (data_dir GLOB '3ds/[a-z0-9]*' AND data_dir NOT GLOB '*[^A-Za-z0-9/_.-]*' AND data_dir NOT GLOB '*..*'
         AND data_dir NOT GLOB '*/' AND data_dir NOT GLOB '*//*' AND data_dir NOT GLOB '3ds/3ds-ua-hub*'
         AND data_key IS NOT NULL AND data_size IS NOT NULL AND data_sha256 IS NOT NULL));

CREATE UNIQUE INDEX variants_data_key ON variants (data_key);
