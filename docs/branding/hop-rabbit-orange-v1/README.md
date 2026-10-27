# Hop — оранжевый прыгающий заяц

Готовый файл: hop-rabbit-orange.png. Большой заяц из исходного эскиза увеличен на полотно, окрашен в оранжевый; надпись Hop и маленький дубликат удалены. Отдельный круглый хвост сохранён как часть исходного символа.

Редактирование выполнено встроенным image_gen с прозрачным фоном. PNG содержит альфа-канал, прозрачность проверена чтением файла. Ресурсы приложения не заменены.

## Основной промпт

```text
Use case: logo-brand / precise-object-edit.
Input image: EDIT TARGET, the supplied Hop logo concept containing a large turquoise leaping rabbit, the word "Hop", and a tiny duplicate rabbit at the bottom.
Extract and preserve ONLY THE LARGE UPPER RABBIT SYMBOL, including its separate small circular tail on the left.
Required edits:
1. Delete the entire "Hop" wordmark and delete the small duplicate rabbit at the bottom.
2. Remove the white background completely. Output must have a GENUINE TRANSPARENT ALPHA BACKGROUND, also in all negative spaces between ears/legs/body and around the separate tail. No white fill, no colored tile, no border, no shadow or checkerboard painted into the image.
3. Recolor the entire retained large rabbit and its tail to ONE SOLID FLAT ORANGE #EF862F. No turquoise, no dark teal, no color gradient, no texture or shading.
4. Enlarge the retained rabbit to fill essentially the WHOLE OUTPUT CANVAS, with only about 2–3 percent transparent safe margin around its silhouette. Use a tightly fitted canvas matching the rabbit's natural width-to-height ratio, approximately 6:5 landscape. Do not leave the old large margins or space reserved for text.
Preserve the exact recognizable large rabbit design: jumping toward the RIGHT, the same long backward-sweeping ears, rounded head/snouted profile, sweeping arched body, long stretched rear leg toward lower-left, stretched front leg toward lower-right, and the separate round tail at the same proportional location. Preserve original silhouette proportions, curve shapes, pose and orientation; do not redesign, simplify away anatomy, add a face/eyes, or change the direction. Smooth crisp vector-like edges and uniform orange interior. ONE rabbit logo only, no letters, no text, no extra symbols, no duplicates. This is a faithful cutout/recolor/enlargement of the large rabbit, not a new rabbit illustration.
```

## Уточнение краёв

```text
Use case: background-extraction / precise-object-edit.
Edit this orange rabbit logo ONLY to clean its alpha and make a production-clean flat logo.
KEEP EXACTLY the same large orange rabbit silhouette, the separate round tail, pose, proportions, scale, tight framing and right-facing orientation. Do not change the rabbit design or add/remove any anatomical feature. No text or duplicate.
REQUIRED CORRECTION: Remove ALL isolated orange/red specks, stray fragments, edge debris, noise and ghost pixels outside the actual rabbit and its circular tail. The entire exterior background must have alpha=0, completely clean transparent. The negative spaces between the ears, below the body, and around the tail must also be fully clean transparent. Rabbit and tail interiors must be uniformly solid orange #EF862F, alpha=255. Along the true silhouette boundary only, use clean smooth antialiasing over a very narrow edge. No glow, halo, texture, gradients, semitransparent wash or detached flecks.
Preserve the existing canvas dimensions and keep the entire rabbit inside frame. Output one crisp flat orange leaping rabbit plus its round tail, filling essentially the canvas with tiny safe margins, on GENUINELY TRANSPARENT background. No white/black canvas or painted checkerboard.
```

