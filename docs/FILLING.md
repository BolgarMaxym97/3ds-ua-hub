# Як додати українізатор у каталог

Каталог наповнюється вручну: файли кладуться в R2, рядки — в D1. Застосунок бачить лише
те, що опубліковано (`published = 1`). Приклад нижче — Super Mario 3D Land 1.0.0, де один
українізатор має два варіанти: європейську й американську гру.

Усі команди запускаються з папки `worker/`. Щоб спершу потренуватися локально, заміни
`--remote` на `--local`, а `wrangler dev` покаже результат на `http://localhost:8787`.

## Що таке «варіант»

Один українізатор — один рядок у `translations`. Кожна гра, під яку він зроблений, — окремий
рядок у `variants` і окремий файл у R2:

| Регіон | Title ID | Код гри | Файл у R2 |
|---|---|---|---|
| EUR | `0004000000053F00` | `CTR-P-AREP` | `super-mario-3d-land/patch/1.0.0/eur.zip` |
| USA | `0004000000054000` | `CTR-P-AREE` | `super-mario-3d-land/patch/1.0.0/usa.zip` |

Застосунок сам знаходить на консолі гру з одним із цих Title ID і ставить відповідний
архів. Якщо такої гри немає, людина вибирає гру вручну, і застосунок підбирає варіант за
кодом гри (`P` = EUR, `E` = USA, `J` = JPN).

## Як лежать файли в R2

Бакет `3ds-ua-hub-files`, корінь — slug гри:

```
super-mario-3d-land/
  banner.png                    400×120, PNG
  icon.png                      48×48, PNG
  patch/1.0.0/eur.zip
  patch/1.0.0/usa.zip
some-cia-game/
  cia/1.0/eur.cia
```

- **slug:** латиниця в нижньому регістрі, цифри й дефіси (`super-mario-3d-land`).
- **Версія в шляху:** обов'язкова. Новий випуск — нова папка, старий файл ніколи не
  перезаписується. Так консоль, що качала 1.0.0, не отримає половину 1.0.1.
- **Інші шляхи:** Worker не віддасть.

## 1. Зібрати архів патча

Архів містить те, що лежить **всередині** `luma/titles/<TID>/`, без самої папки з Title ID:

```bash
cd <переклад>/dist/eur/luma/titles/0004000000053F00
zip -qrXD ~/hub-upload/eur.zip .

cd <переклад>/dist/usa/luma/titles/0004000000054000
zip -qrXD ~/hub-upload/usa.zip .
```

- **`-X -D`:** без macOS-атрибутів і записів папок, лишаються тільки файли.
- **`unzip -l eur.zip`:** має показати `locale.txt` і `romfs/...` у корені.
- **Що може бути в архіві:**
  - `romfs/...` — замінені файли гри;
  - `code.ips` — патч коду, якщо є;
  - `locale.txt` — якщо є;
  - `check.txt` — необов'язково, див. «Для ручного вибору гри».

**CIA** кладеться як є: `some-cia-game/cia/1.0/eur.cia`.

## 2. Порахувати розмір і sha256

```bash
cd ~/hub-upload
for f in *.zip banner.png icon.png; do echo "$f  $(stat -f %z "$f")  $(shasum -a 256 "$f" | cut -c1-64)"; done
```

Розмір і хеш потрапляють у підписаний каталог. Застосунок відкине файл, якщо хоч один байт
не збігся. Тому **рахуй після останньої зміни файлу** й завантажуй саме цей файл. Якщо
перезібрав zip, хеш зміниться: дата файлів пишеться всередину.

## 3. Завантажити файли в R2

```bash
npx wrangler r2 object put 3ds-ua-hub-files/super-mario-3d-land/patch/1.0.0/eur.zip --file ~/hub-upload/eur.zip --remote
npx wrangler r2 object put 3ds-ua-hub-files/super-mario-3d-land/patch/1.0.0/usa.zip --file ~/hub-upload/usa.zip --remote
npx wrangler r2 object put 3ds-ua-hub-files/super-mario-3d-land/banner.png --file ~/hub-upload/banner.png --remote
npx wrangler r2 object put 3ds-ua-hub-files/super-mario-3d-land/icon.png  --file ~/hub-upload/icon.png  --remote
```

Можна й мишкою в Cloudflare Dashboard → R2 → `3ds-ua-hub-files` → Upload. Тоді папки
створюй руками, точно як у шляху вище.

## 4. Банер та іконка

- **Банер:** рівно 400×120 PNG (10:3), на всю ширину верхнього екрана (400×240). Назву гри й
  регіони застосунок пише сам на темній плашці внизу ліворуч, а тип (патч чи CIA) — у правому
  верхньому куті. Тому логотип у кадрі не потрібен, краще сцена з героями, і важливе не повинно
  лежати в лівому нижньому куті. З великої картинки (напр. 1600×800) виріж прямокутник 10:3 і
  зменш:
  ```bash
  magick img.png -crop 950x285+650+150 +repage -resize '400x120!' -strip banner.png
  ```
  `-crop ШxВ+X+Y` — прямокутник у вихідній картинці; Ш:В має бути 10:3 (950×285, 1000×300,
  1600×480). Подивись результат перед завантаженням.
- **Іконка:** 48×48 PNG, як у меню HOME (у списку застосунок показує її 26 px, на сторінці гри 32 px). Найпростіше взяти іконку гри з її SMDH (GodMode9 →
  гра → `Show title info`, або FBI) і зберегти як PNG.
- **Якщо поки немає:** у SQL пиши `NULL` замість хешу, і застосунок покаже кольоровий квадрат
  з літерами.

## 5. Записати рядки в D1

Візьми шаблон `docs/examples/super-mario-3d-land.sql`, підстав розміри й хеші з кроку 2,
збережи копію й виконай:

```bash
npx wrangler d1 execute 3ds-ua-hub --remote --file=~/hub-upload/super-mario-3d-land.sql
```

Поля `translations`:

| Поле | Що писати |
|---|---|
| `slug` | як у R2 |
| `title` | назва гри, як люди її шукають |
| `kind` | `patch` (на SD, потрібна гра) або `cia` (окрема гра) |
| `version` | версія **перекладу**; та сама, що в шляху R2 |
| `text_pct`, `textures_pct` | 0–100 або `NULL` — тоді в застосунку «невідомо» |
| `third_party` | `1` для чужих українізаторів |
| `changelog` | що нового у цій версії, 1–2 речення |
| `min_app` | `NULL` — ставиться будь-яким Hub. Версія (`0.2.0`), якщо переклад потребує новішого Hub: старіші його показують, але «Встановити» заблоковано з «Потрібна v0.2.0» |
| `published` | `0` чернетка, `1` у каталозі |

Поля `variants`:

| Поле | Що писати |
|---|---|
| `region` | `EUR`, `USA`, `JPN`, `KOR`, `CHN`, `TWN` |
| `title_id` | 16 символів, **великі** літери. Для CIA — Title ID самої CIA |
| `product_code` | код гри, напр. `CTR-P-AREP`; для ручного вибору |
| `game_version` | `NULL` = будь-яка; число = потрібна версія оновлення гри у форматі AM (`major·1024 + minor·16 + micro`, тож v1.1.0 = 1040; дивись нижче) |
| `file_key`, `file_size`, `file_sha256` | з кроків 2–3; sha256 **малими** літерами |
| `data_key`, `data_size`, `data_sha256`, `data_dir` | лише для порту з файлами на SD-карті, див. нижче; інакше `NULL` |

База сама відкине типові помилки: малі літери в Title ID, хеш не тієї довжини, відсоток понад 100,
пробіл у slug, варіант для неіснуючого slug.

## 6. Опублікувати й перевірити

```bash
npx wrangler d1 execute 3ds-ua-hub --remote --command "UPDATE translations SET published = 1 WHERE slug = 'super-mario-3d-land'"
curl -s https://3ds-ua-hub.<акаунт>.workers.dev/v1/catalog | python3 -m json.tool | head -40
curl -sI https://3ds-ua-hub.<акаунт>.workers.dev/v1/f/super-mario-3d-land/patch/1.0.0/eur.zip
```

У каталозі мають бути обидва варіанти, а `HEAD` на кожен файл має повернути `200` і той самий
`Content-Length`, що в базі. Перевірити все одним запитом:

```bash
npx wrangler d1 execute 3ds-ua-hub --remote --command "SELECT slug, region, title_id, file_key, file_size FROM variants ORDER BY slug, region"
```

## Окрема гра з файлами на SD-карті (порт)

Деякі порти — CIA-рушій, який читає саму гру з теки на SD-карті (так працює butterscotch для
Undertale: `SD:/3ds/butterscotch/undertale/`). Такий варіант має, крім CIA, ще й **дані**: zip,
який застосунок качає після програми й розпаковує в `data_dir`.

- **Архів даних:** вміст теки гри без самої теки (`data.win`, `mus_*.ogg`, `cache/...` у корені zip),
  `zip -qrXD`. Ключ у R2: `<slug>/data/<version>/<region>.zip`.
- **`data_dir`:** шлях від кореня SD-карти, тільки під `3ds/`, без `/` на кінці й без `..`, не
  `3ds/3ds-ua-hub`. Напр. `3ds/butterscotch/undertale`.
- **Збереження** гри, якщо рушій пише їх у ту саму теку, застосунок не чіпає: при оновленні
  переносить у нову версію, при видаленні лишає.

```sql
INSERT INTO variants (slug, region, title_id, file_key, file_size, file_sha256,
                      data_key, data_size, data_sha256, data_dir) VALUES
  ('undertale', 'EUR', '0004000000EFAD00', 'undertale/cia/1.0/eur.cia', 2280896, '<sha256 cia>',
   'undertale/data/1.0/eur.zip', 142154315, '<sha256 zip>', '3ds/butterscotch/undertale');
```

`data_dir` задається тільки разом з трьома іншими полями даних (база не пропустить інакше).
На консолі потрібно місця: CIA + zip + розпаковані файли.

## Новий випуск перекладу

1. Новий архів у **нову** папку: `super-mario-3d-land/patch/1.0.1/eur.zip`.
2. У SQL: `version = '1.0.1'`, нові `file_key`, `file_size`, `file_sha256`, `changelog`.
3. Виконати SQL (шаблон сам замінює рядки цього slug), `published = 1`.
4. Консолі з 1.0.0 побачать «Є оновлення» при наступному запуску.

Старі файли можна лишити в R2 або видалити, коли ніхто вже не качає (`wrangler r2 object delete`).

## Для ручного вибору гри: `check.txt` (необов'язково)

Якщо людина вибирає гру вручну, застосунок може переконатися, що файли гри ті самі, під які
зроблено переклад. Для цього в архів кладеться `check.txt`: sha256 **оригінальних** файлів,
які замінює патч. Береться з власного розпакованого дампу:

```bash
cd ~/dumps/super-mario-3d-land-eur     # тут romfs/ оригінальної гри
shasum -a 256 romfs/LocalizedData/EuRussian/LayoutData/Font.szs \
              romfs/LocalizedData/EuRussian/MessageData/SystemMessage.szs \
              romfs/LocalizedData/EuRussian/MessageData/LayoutMessage.szs > check.txt
```

Без `check.txt` ручний вибір усе одно працює. Тоді застосунок завжди попереджає «на свій ризик».

## Код гри й версія оновлення: звідки взяти

- **Код гри:** FBI → Titles → гра → Product code. Або GodMode9 → `Show title info`. Остання
  літера коду (`CTR-P-ARE`**`P`**) — регіон: `P` EUR, `E` USA, `J` JPN, `K` KOR, `C` CHN, `W` TWN.
- **Версія оновлення:** FBI → Titles → оновлення гри (`0004000E…`) → Version. Якщо FBI показує
  `1.1.0`, переведи в число: `1·1024 + 1·16 + 0 = 1040`.

## Типові помилки

| Що бачиш | Причина |
|---|---|
| `CHECK constraint failed` | опечатка: Title ID малими літерами, хеш не 64 символи, відсоток понад 100 |
| `FOREIGN KEY constraint failed` | варіант додано раніше за рядок у `translations` |
| у каталозі порожньо | `published = 0` |
| файл `404` | шлях у R2 не збігається з `file_key` (регістр, версія, `.zip`) |
| застосунок пише «Файл пошкодився» | sha256 чи розмір у базі від іншої збірки zip |
