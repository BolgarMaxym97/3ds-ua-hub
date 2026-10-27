-- Super Mario 3D Land 1.0.0: one translation, a variant per region.
-- Fill <...> from the commands in docs/FILLING.md, then:
--   npx wrangler d1 execute 3ds-ua-hub --remote --file=../docs/examples/super-mario-3d-land.sql
-- Re-running it is safe: it replaces the rows of this slug.

INSERT OR REPLACE INTO translations
  (slug, title, kind, version, released_at, description, changelog, authors, authors_url,
   text_pct, textures_pct, third_party, min_app, banner_sha256, icon_sha256, published, updated_at)
VALUES
  ('super-mario-3d-land', 'Super Mario 3D Land', 'patch', '1.0.0', '2026-10-20',
   'Перекладено всю гру: меню, підказки, діалоги, повідомлення системи й заголовки рівнів. У шрифтах справжні і, ї, є.',
   'Перший випуск.',
   'Side Quest', 'https://t.me/side_quest_ua',
   100, 100, 0, NULL,
   '<BANNER_SHA256>', '<ICON_SHA256>',   -- NULL, якщо банера чи іконки ще немає
   0,                                     -- 0: чернетка. Опублікувати: docs/FILLING.md, крок 6
   strftime('%Y-%m-%dT%H:%M:%SZ', 'now'));

DELETE FROM variants WHERE slug = 'super-mario-3d-land';

INSERT INTO variants (slug, region, title_id, product_code, game_version, file_key, file_size, file_sha256) VALUES
  ('super-mario-3d-land', 'EUR', '0004000000053F00', 'CTR-P-AREP', NULL,
   'super-mario-3d-land/patch/1.0.0/eur.zip', <EUR_SIZE>, '<EUR_SHA256>'),
  ('super-mario-3d-land', 'USA', '0004000000054000', 'CTR-P-AREE', NULL,
   'super-mario-3d-land/patch/1.0.0/usa.zip', <USA_SIZE>, '<USA_SHA256>');
