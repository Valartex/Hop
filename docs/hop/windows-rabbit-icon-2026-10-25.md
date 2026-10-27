# Значок Hop с зайцем

0.2.0.17: по указанию пользователя выбран
docs/branding/hop-rabbit-windows-ico-v1/Hop-Rabbit.ico. Рабочий hop.ico является
его побайтной копией. SHA256 исходного ICO:
34485effec1f5aabb6d2d05a03da106f3bfac06e130dc786f528c0e636db97c1.

Значок окна, панели задач, трея и уведомлений использует встроенный ICO.
Для бокового меню и «О Hop» встроен 256px PNG из выбранного ICO; пропорции
и прозрачность сохранены. render-hop-icons.py синхронизирует эти ресурсы,
не пересобирая и не меняя кадры ICO. Предпросмотр PNG просмотрен.

Сборка Hop успешна: .research/build-0217.log. Все 15 кадров нативного PE-ресурса
побайтно совпали с исходным ICO: .research/verify-0217-icon.py,
.research/icon-0217-verified.json. Дополнительные тесты сетевой логики не
запускались: изменены только изображения, ссылки на ресурсы и версия клиента.

Основной ПК обновлён после UAC 25 октября 2026 в 13:33:55 МСК (10:33:55 UTC).
Результат .research/local-hop-update-0.2.0.17-result.json: completed=true,
filesVerified=198, ipcListening=true. После helper все 198 файлов независимо
сверены. Хеш финального Hop.exe и установленной копии:
687eb727abb0da7d165b84b8ff55dcdc49004a7baf0f02da605bf8896bf23a47.

Служба Running с прежним PID; serviceUnchanged, networkUnchanged,
amneziaServicesUnchanged=true. GUI/VPN не запускались. Резервная копия:
.research/local-backup-before-0.2.0.17. Проводнику отправлено стандартное
уведомление SHCNE_UPDATEITEM об изменении Hop.exe для обновления значка.
