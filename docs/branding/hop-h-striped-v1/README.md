# Hop — оранжевая дуга и горизонтальные прорези

Два варианта: фон текущего Hop и зелёный фон uTorrent. Точки и надпись отсутствуют, четыре горизонтальные прорези выполнены в цвет фона, оранжевая траектория не разрезана. Зазор вокруг неё также совпадает с фоном.

## Готовые варианты для просмотра

- 01-hop-background.svg и 01-hop-background-1024.png — мятно-бирюзовый градиент текущего Hop.
- 02-utorrent-green.svg и 02-utorrent-green-1024.png — зелёный фон #76B83F.
- Файлы *-256.png — дополнительный размер.
- comparison.png — сравнение двух вариантов на нейтральном фоне.

Скругление и положение плашки скопированы непосредственно из client/images/hop/mark.svg: viewBox 64×64; x=2, y=2, width=60, height=60, rx=18. Градиент первого варианта также взят из этого SVG: #8cebdd → #42bfc2. Зелёный #76B83F взят из [официального логотипа uTorrent](https://cdn-assets.bittorrent.com/knowledge-panel/uTorrent_Logo.svg), найденного на [официальном сайте](https://www.utorrent.com/).

Рабочий процесс: эскизы созданы встроенным image_gen, затем геометрия отдельно отрисована в SVG для точного скругления, чистых прозрачных углов и совпадения фона с прорезями. imagegen-reference-1.png и imagegen-reference-2.png — промежуточные растровые эскизы, а не окончательные файлы. render.py воспроизводит SVG и PNG через существующий QtSvg. Прозрачность углов и непрозрачность плашки проверены, сравнение просмотрено. Ресурсы приложения не заменены; это этап выбора оформления, проверка установленного трея не выполнялась.

## Промпты встроенного image_gen

### Вариант 1

```text
Use case: logo-brand, precise icon edit.
Input image 1: EDIT TARGET, the selected Hop logo with dark uppercase H and orange perspective jumping trajectory.
Input image 2: REFERENCE ONLY for the existing Hop app icon's mint/turquoise gradient background and generously rounded-square tile corners. Do NOT copy its old thin H symbol.
Create a refined variant of image 1 with these exact changes:
- Keep its thick DARK BLUE-GREEN uppercase H, original straight horizontal crossbar, large shape, the same curved ORANGE trajectory, the same strong taper in perspective, same lack of dots or lettering.
- Slice ONLY the DARK LETTER H with EXACTLY FOUR clean, parallel, equally spaced HORIZONTAL background-colored gaps spanning the H from its leftmost edge to its rightmost edge. These are true cutouts that reveal the actual gradient tile behind the letter, NOT white or colored added lines. The cuts divide the dark letter into horizontal stacked bands. Cuts are clearly visible, moderately broad (each about 3 percent of the letter height), simple geometry. Orange trajectory is NOT sliced; it remains one smooth unbroken curved strip in front.
- Replace the pale gray background with the EXACT MINT-TO-TURQUOISE gradient of reference image 2, pale mint #8CEBDD in the upper left transitioning gently toward turquoise #42BFC2 at the lower right. Make this the background of a generously rounded-square app-icon TILE.
- Match the reference tile's CORNER ROUNDING and inset: on a 64x64 normalized canvas the tile spans x=2,y=2,width=60,height=60 with corner radius=18. Scale this exact rounded-square shape to the image size. Outside those rounded corners is genuinely TRANSPARENT, not white, black or a painted checkerboard.
- The narrow clearance on both sides of the orange trajectory must reveal the SAME mint/turquoise gradient background at that location. Absolutely NO WHITE OUTLINE, ring or border around the orange curve or letter.
The dark H remains large and centered within the tile with safe padding, the orange arc must fit fully inside. Dark H color #102932, orange arc #EF862F. Retain H silhouette and arc identity. No dots, circles, text, captions, gradients on the letter or arc, shadows, 3D, mockup or decorative outline. Only the TILE background has a subtle gradient, as in the actual Hop reference. One icon, frontal flat vector-style design.
```

### Вариант 2

```text
Use case: logo-brand, precise palette edit.
Image 1 is the striped Hop icon EDIT TARGET; image 2 is a geometric reference for the current Hop app icon's exact rounded-square tile silhouette only.
Keep image 1's dark H, FOUR horizontal background cutouts, uninterrupted orange jumping trajectory, no dots, and all composition unchanged.
Change ONLY the mint/turquoise background tile to uTorrent's recognizable solid fresh medium GREEN #76B83F. This exact green is sampled from the official uTorrent logo SVG. Do not use any uTorrent symbol or lettering.
All horizontal cutouts in H and clearance around the orange trajectory must become the SAME GREEN #76B83F as the tile, so they look like background revealed through the dark letter. No white outlines anywhere.
Keep H dark blue-green #102932 and orange trajectory #EF862F.
Match image 2's strongly rounded tile silhouette precisely: normalized 64x64 canvas, tile x=2 y=2 width=60 height=60 radius=18 (radius is 30% of tile width), with smooth clean vector edges. Rounded corners must be more generous than image 1 if needed to match image 2. Genuinely transparent outside the tile. Remove all edge speckles, stray pixels, ragged halos and turquoise debris outside tile. No texture, no noise, no shadows, no 3D, no text. One polished flat app icon.
```

