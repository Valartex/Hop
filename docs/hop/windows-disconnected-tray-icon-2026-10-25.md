# Мятный значок трея без подключения

0.2.0.18: docs/branding/hop-rabbit-mint-windows-ico-v1/Hop-Rabbit-Mint.ico
встроен побайтной копией как :/images/hop/hop-disconnected.ico. SHA256:
fa88f3626c745f6c2bb1adf1e9ea626e2bffb5942a702bc346a19679f55bc93b.

В Windows-трее Hop оранжевый заяц используется только при Vpn::Connected.
Disconnected, Preparing, Connecting, Reconnecting, Disconnecting, Error и Unknown
показывают мятного зайца. При запуске без подключения сразу выбран мятный значок.
QIcon сохраняет все 15 кадров для выбора по DPI. Нативный значок EXE, окна,
бокового меню и «О Hop» остаётся оранжевым.

В существующую проверку трея добавлены сравнение изображений обоих состояний,
начальное состояние, переход Connected → каждое состояние без подключения и
сохранение 15 размеров. Проверка не запускает VPN и не меняет сеть.

Сборка успешна: .research/build-0218.log, .research/build-0218-final.log.
Целевая trayConnectionDetailsAndTranslation прошла за 302 мс, включая проверку
начального значка, перехода Connected → каждое из семи остальных состояний,
изображений обоих цветов, 15 размеров и существующей проверки подсказки/перевода.
Лог: .research/tray-0218-test.txt (3 passed с init/cleanup, 0 failed).
Мятный ICO совпадает с исходным файлом. Все 15 нативных кадров оранжевого значка
EXE также совпали с прежним источником: .research/icon-0218-orange-verified.json.
Сетевые сценарии VM не повторялись: сетевой код не менялся.

Основной ПК обновлён после UAC 25 октября 2026 в 14:05:26 МСК (11:05:26 UTC).
Результат .research/local-hop-update-0.2.0.18-result.json: completed=true,
filesVerified=198, ipcListening=true. После helper все 198 файлов независимо
сверены. Хеш финального Hop.exe и установленной копии:
cda9831c483d3a0c0a4213d7d7d5c28130324c4a73ddc519934d969b09601a0c.

Служба Running с прежним PID; serviceUnchanged, networkUnchanged,
amneziaServicesUnchanged=true. GUI/VPN не запускались. Резервная копия:
.research/local-backup-before-0.2.0.18.
