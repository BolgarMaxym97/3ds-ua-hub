# Розгортання Worker (вручну, один раз)

Worker `3ds-ua-hub` віддає каталог, файли й приймає статистику. Код — у `worker/`, усі
команди звідти.

## Розробка

```bash
npm install
npm test                 # Vitest у рантаймі Workers, локальні D1 і R2
npm run typecheck
npx wrangler d1 migrations apply 3ds-ua-hub --local    # один раз перед `npm run dev`
npm run keygen && printf 'SIGNING_KEY=%s\n' "$(cat .signing-key.json)" > .dev.vars && rm .signing-key.json   # тестовий ключ для dev
npm run dev              # http://localhost:8787
```

## Перше розгортання

```bash
npx wrangler login

npx wrangler d1 create 3ds-ua-hub
```

Команда друкує `database_id`. Встав його у `wrangler.jsonc` замість нулів. Коли Wrangler
запропонує додати базу сам, відповідай «no», бо зв'язка вже прописана.

```bash
npx wrangler r2 bucket create 3ds-ua-hub-files
npx wrangler d1 migrations apply 3ds-ua-hub --remote     # очікуй 0001_catalog.sql ✅, 0002_events.sql ✅

npm run keygen
```

`keygen` робить дві речі:

1. **Приватний ключ** записує у файл `worker/.signing-key.json`. Файл git не бачить, а на
   екран ключ не виводиться, тож його неможливо випадково вставити в чат чи issue. Задай
   його Worker, збережи в менеджері паролів і видали з диска:
   ```bash
   npx wrangler secret put SIGNING_KEY < .signing-key.json
   rm .signing-key.json
   ```
2. **Публічний ключ (32 байти, hex)** друкує на екран. Він не секрет: його вшиваємо в
   застосунок (`app/source/catalog_key.h`).

Ключ генерується один раз. Новий ключ = нова версія застосунку, бо старі консолі перестануть
довіряти каталогу.

```bash
npx wrangler deploy                                      # друкує https://3ds-ua-hub.<акаунт>.workers.dev
curl http://3ds-ua-hub.<акаунт>.workers.dev/v1/health    # очікуй: ok
curl -sI http://3ds-ua-hub.<акаунт>.workers.dev/v1/catalog | grep -i x-signature
```

Адреса `http://3ds-ua-hub.<акаунт>.workers.dev` іде в застосунок (`HUB_URL` в
`app/source/paths.h`). Змінити її потім можна лише новою версією застосунку.

## Чому HTTP, а не HTTPS

Вбудований TLS 3DS не може підключитися до workers.dev, це перевірено на 3ds-ua-stats. Тому
консоль ходить по HTTP, але нічого не приймає на віру:

- каталог підписаний Ed25519, а застосунок перевіряє підпис вшитим публічним ключем;
- у каталозі є sha256 кожного файлу, і архів з іншим хешем застосунок не встановить.

## Статистика

```bash
# усе про одну консоль (ID з екрана «Допомога», без дефіса)
npx wrangler d1 execute 3ds-ua-hub --remote --command \
  "SELECT received_at, event, slug, variant_region, title_id, manual, risky, error_code, error_stage FROM events WHERE install_id = 'K7QM29XD' ORDER BY received_at"

# встановлення за українізаторами
npx wrangler d1 execute 3ds-ua-hub --remote --command \
  "SELECT slug, COUNT(DISTINCT install_id) AS consoles FROM events WHERE event = 'install' GROUP BY slug ORDER BY consoles DESC"

# які ігри люди вибирають вручну: кандидати в нові Title ID
npx wrangler d1 execute 3ds-ua-hub --remote --command \
  "SELECT slug, title_id, COUNT(*) FROM events WHERE manual = 1 GROUP BY slug, title_id ORDER BY 3 DESC"
```

Workers Logs вимкнено (`observability.enabled: false`), тож Cloudflare теж не зберігає IP.
