# Hop — четыре знака с фоном, без точек

Создано встроенным image_gen из четырёх эскизов hop-h-perspective-v2. По последнему уточнению пользователя все круглые точки и их обводки удалены. Надпись Hop отсутствует. Фон включён в PNG.

1. 01-teal-dark-bg.png — бирюзовая H и белая сужающаяся траектория на тёмном фоне.
2. 02-two-tone-light-bg.png — тёмная H с прямой перекладиной, бирюзовая дуга на светлом фоне.
3. 03-curved-teal-dark-bg.png — бирюзовая H, более изогнутая белая дуга от края до края, тёмный фон.
4. 04-arc-crossbar-light-bg.png — две тёмные стойки, бирюзовая дуга вместо прямой перекладины, светлый фон.

Растровые эскизы проверены визуально. Ресурсы приложения не заменены; проверка в реальном трее и векторизация не выполнялись.

## Точные промпты

### Вариант 1

```text
Use case: logo-brand. Precise edit of the supplied logo. User requests removal of ALL DOTS from ALL versions, while keeping a solid icon background. DELETE BOTH circular endpoint dots completely. No circular holes, no disks, no rings, no halos, no knobs or rounded circular terminals anywhere. Remove the white circular separation rings too, restoring the uninterrupted original H pillar or background wherever the dots and rings were. KEEP THE TAPERED JUMPING ARC as a flowing curved strip, ending with clean ordinary cut edges, never a bulb or a circle. Preserve the same curve, perspective taper from wide lower-left near end to thin upper-right far end, H proportions, corner radii, symbol size and centered layout of the reference. Only the circles and their rings are removed; don't redesign the logo. No text or wordmark. ONE centered large symbol with comfortable margins on a SQUARE OPAQUE FULL-BLEED FLAT BACKGROUND. This background covers every pixel of the canvas; no white exterior margin or floating tile. Crisp flat vector-like logo, no gradients, textures, shadows, glow or 3D.
VERSION 1: Keep the turquoise H #00AFA5 with its central connecting bridge and WHITE negative-space-style tapered arc across its middle. Replace the two WHITE CIRCLE HOLES inside H with solid TURQUOISE matching the letter; no circle outlines remain. Keep only the white tapered curved strip between their former locations. Whole-canvas background is solid dark blue-green #102932, also visible in the empty spaces above and below the H bridge. The retained arc stays solid white, not background-colored.
```

### Вариант 2

```text
Use case: logo-brand. Precise edit of the supplied logo. User requests removal of ALL DOTS from ALL versions, while keeping a solid icon background. DELETE BOTH circular endpoint dots completely. No circular holes, no disks, no rings, no halos, no knobs or rounded circular terminals anywhere. Remove the white circular separation rings too, restoring the uninterrupted original H pillar or background wherever the dots and rings were. KEEP THE TAPERED JUMPING ARC as a flowing curved strip, ending with clean ordinary cut edges, never a bulb or a circle. Preserve the same curve, perspective taper from wide lower-left near end to thin upper-right far end, H proportions, corner radii, symbol size and centered layout of the reference. Only the circles and their rings are removed; don't redesign the logo. No text or wordmark. ONE centered large symbol with comfortable margins on a SQUARE OPAQUE FULL-BLEED FLAT BACKGROUND. This background covers every pixel of the canvas; no white exterior margin or floating tile. Crisp flat vector-like logo, no gradients, textures, shadows, glow or 3D.
VERSION 2: Keep the dark blue-green H #073641 WITH ITS EXISTING STRAIGHT HORIZONTAL CROSSBAR. Keep the turquoise #00AFA5 tapered curved strip crossing in front, with its thin white clearance border. Remove both turquoise endpoint circles AND their surrounding white circular outlines. Reconstruct the dark pillar under any removed disk where needed; outside the H restore the background. Retain the arc endpoints without adding circles. Background is uniform pale mint #D6E9E4 across the whole canvas.
```

### Вариант 3

```text
Use case: logo-brand. Precise edit of the supplied logo. User requests removal of ALL DOTS from ALL versions, while keeping a solid icon background. DELETE BOTH circular endpoint dots completely. No circular holes, no disks, no rings, no halos, no knobs or rounded circular terminals anywhere. Remove the white circular separation rings too, restoring the uninterrupted original H pillar or background wherever the dots and rings were. KEEP THE TAPERED JUMPING ARC as a flowing curved strip, ending with clean ordinary cut edges, never a bulb or a circle. Preserve the same curve, perspective taper from wide lower-left near end to thin upper-right far end, H proportions, corner radii, symbol size and centered layout of the reference. Only the circles and their rings are removed; don't redesign the logo. No text or wordmark. ONE centered large symbol with comfortable margins on a SQUARE OPAQUE FULL-BLEED FLAT BACKGROUND. This background covers every pixel of the canvas; no white exterior margin or floating tile. Crisp flat vector-like logo, no gradients, textures, shadows, glow or 3D.
VERSION 3: Keep the turquoise H #00AFA5 with its existing central bridge and MORE BOWED white tapered curved strip. The source includes two TURQUOISE DISKS protruding from the left and right outer H edges, each with a WHITE circular ring. REMOVE BOTH DISKS AND BOTH RINGS COMPLETELY. Restore the H's straight uninterrupted outside pillar edges where they were overlapped by rings. Keep only the curved tapered WHITE trajectory, with ordinary cut endpoints near the outer sides of the H, no circles or circular holes. Background is solid dark blue-green #102932 across the whole canvas and between the H stems. Preserve the more curved trajectory of this reference.
```

### Вариант 4

```text
Use case: logo-brand. Precise edit of the supplied logo. User requests removal of ALL DOTS from ALL versions, while keeping a solid icon background. DELETE BOTH circular endpoint dots completely. No circular holes, no disks, no rings, no halos, no knobs or rounded circular terminals anywhere. Remove the white circular separation rings too, restoring the uninterrupted original H pillar or background wherever the dots and rings were. KEEP THE TAPERED JUMPING ARC as a flowing curved strip, ending with clean ordinary cut edges, never a bulb or a circle. Preserve the same curve, perspective taper from wide lower-left near end to thin upper-right far end, H proportions, corner radii, symbol size and centered layout of the reference. Only the circles and their rings are removed; don't redesign the logo. No text or wordmark. ONE centered large symbol with comfortable margins on a SQUARE OPAQUE FULL-BLEED FLAT BACKGROUND. This background covers every pixel of the canvas; no white exterior margin or floating tile. Crisp flat vector-like logo, no gradients, textures, shadows, glow or 3D.
VERSION 4: Keep the TWO dark blue-green #073641 vertical pillars and turquoise #00AFA5 tapered jumping arc with thin white clearance. Remove the two turquoise dots and both circular white rings completely, restoring the pillar/background underneath. There must still be NO STRAIGHT HORIZONTAL CROSSBAR: between the pillars, the space below the arc stays completely OPEN BACKGROUND down to the bottom. The arc alone plays the role of H's crossbar. Preserve the source geometry and no-crossbar design. Whole-canvas background is uniform pale mint #D6E9E4.
```

