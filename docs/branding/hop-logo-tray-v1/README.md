# Hop — версия знака для трея, v1

Доработка выбранного второго варианта, 23 октября 2026.

Дуга и две точки сохранены. Для размеров 16–24 px точки увеличены, дуга утолщена до 2,5 px в сетке 16 px, зазоры расширены. Шрифтовая подпись в маленьком значке не используется. Все SVG/PNG имеют прозрачный фон.

## Файлы

- tray-size-preview.png — знак, показ на светлом/тёмном фоне в размерах 16, 20, 24, 32 px и увеличенные пиксели 16 px. Фактические размеры верны при просмотре картинки в масштабе 100%.
- hop-teal.svg — основной векторный знак; hop-teal-micro.svg — вариант для малых размеров.
- hop-light-bg.svg / hop-light-bg-micro.svg — тёмный знак для светлого фона.
- hop-dark-bg.svg / hop-dark-bg-micro.svg — светлый знак для тёмного фона.
- hop-{teal,light-bg,dark-bg}-{size}.png — отдельная растеризация каждого размера 16, 20, 24, 32, 40, 48, 64, 128, 256 px.
- hop-{teal,light-bg,dark-bg}.ico — ICO с теми же девятью размерами, включая адаптированные малые кадры.
- imagegen-reference.png — промежуточный растровый референс, полученный встроенным image_gen. Векторный знак построен отдельно и экспортирован через QSvgRenderer.
- render.py — воспроизводимое создание SVG/PNG/ICO и таблицы предпросмотра. Использует существующий .tools/preview-runtime.
- pixel-check.txt — проверка, что при пороге alpha >= 128 и восьмисвязности в размерах 16/20/24/32 остаются ровно три отдельных элемента. Это техническая проверка разделения элементов; окончательная визуальная оценка выполняется по предпросмотру.

Ресурсы приложения, обработчик трея, сборка и установленная версия не менялись. В текущем обработчике setTrayIcon исходный PNG принудительно масштабируется до 128 px; при внедрении следует передавать QIcon с подготовленными кадрами и проверить реальный трей Windows при разных DPI. Предпросмотр не является проверкой установленного приложения.

## Промпт встроенного image_gen

```text
Use case: logo-brand. Input image is the selected Hop VPN logo concept, edit target. Refine ONLY the two-endpoint jumping-arc symbol for excellent small Windows tray icon readability. Preserve its identity, rising diagonal left-to-right motion, two separated solid round location dots, and one solid curved jump arc. Enlarge both endpoint dots slightly to equal sizes, thicken the arc, make the two white gaps between arc and endpoints substantially wider and equal. Compact balanced square silhouette, no small details. Keep this a SINGLE symbol made of exactly three disconnected solid shapes. Use one solid dark teal #073641, no gradients or textures. Remove the wordmark, bottom duplicate and white background. Center the one refined symbol on a genuinely transparent canvas with generous clear padding. No text, labels, additional symbols, arrows, borders, shadows, 3D or mockup. This is an isolated production design reference for a native SVG and pixel-specific icon set; no fake pixel grids.
```

