# Проверка постоянной защиты Windows

VM Windows 10, тестовый агент запущен пользователем от администратора.
Amnezia VPN на хосте не отключалась. Исходная ручная проверка: см. `vm-validation-2026-10-14.md`.

Подтверждённый запуск `325aab21fd894817bf4167b585b4c60b`, 19:24–19:25 MSK:

```
Xray SOCKS HTTPS: HTTP 204, Qt 0
TUN IPv4 HTTPS: HTTP 204, Qt 0
TUN DNS HTTPS: HTTP 204, Qt 0
Direct IP with VPN active: HTTP 200, Qt 0
Persistent filters after failure cleanup: 16
Protected IP after tunnel failure: HTTP 0, Qt 99
Direct IP with protection retained: HTTP 200, Qt 0
Tunnel reconnected before active-service crash
Xray SOCKS HTTPS: HTTP 204, Qt 0
TUN IPv4 HTTPS: HTTP 204, Qt 0
Direct IP with protection retained: HTTP 200, Qt 0
Owned test service terminated
Protected IP after service crash: HTTP 0, Qt 99
Direct IP with protection retained: HTTP 200, Qt 0
Hop protection filters remaining: 0
Network after disconnect: HTTP 204, Qt 0
```

Exit code теста и аварийной очистки: 0. Журналы и манифест сборки сохранены локально
в `.research/vm-runs/agent-325aab21fd894817bf4167b585b4c60b`.
Ранее один расширенный запуск прошёл первую фазу, но получил timeout первого TUN
запроса после reconnect; добавлена отдельная проверка SOCKS после reconnect для
разделения этапов. Это требует дальнейшего тестирования стабильности.

Постоянный guard применяется до запуска Xray, затем атомарно добавляется разрешение
на собственный TUN. Транспорт разрешён только процессу службы к IP/порту сервера.
Direct-исключения выше общего запрета, DNS физического интерфейса блокируется выше
Direct, разрешение TUN выше DNS. Ошибка или реконнект не снимают guard; явное
отключение снимает его после очистки ресурсов. При неудачной WFP-транзакции остаётся
предыдущий набор правил.

Не подтверждены: BFE/перезагрузка, boot-time защита, AWG, UDP, полная DNS/IPv6
приёмка, Direct-разрешение новых доменов при недоступном VPN, SCM/GUI и установщик.
Наличие постоянных правил само по себе не доказывает защиту до запуска BFE.
