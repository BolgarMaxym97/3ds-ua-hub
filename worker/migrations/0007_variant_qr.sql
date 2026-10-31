-- A patch's game for whoever does not have it and that the Hub cannot download itself (no
-- fallback_url): a link to the game's page on its storage. The app shows it as a QR code (J8), the
-- person opens it on a phone, gets the CIA there and installs it. The page itself, not a file.
ALTER TABLE variants ADD COLUMN qr_url TEXT
  CHECK (qr_url IS NULL OR ((qr_url GLOB 'http://?*' OR qr_url GLOB 'https://?*') AND length(qr_url) <= 200
                            AND qr_url NOT GLOB '*[^!-~]*'));
