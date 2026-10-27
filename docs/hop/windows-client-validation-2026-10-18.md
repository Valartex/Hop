# Проверка Windows-клиента 18 октября 2026

После описанных ниже проверок пользователь обнаружил консоль при обычном запуске.
Она исправлена в новой сборке daf28fb6; две затронутые нативные регрессии и
GUI BlancVPN/AWG повторно прошли, noConsole=true. Актуальные хеши и результаты:
[отчёт исправления консоли](windows-gui-console-fix-2026-10-18.md).

Объём: исходные функции VPN-клиента, Windows, IPv4 с блокировкой IPv6.
Установщик/CI/публикация не входят в эту работу. Новые бинарники проверяются
напрямую в уже существующем защищённом каталоге Windows 10 VM через ручной агент.
Amnezia, сеть, службы и драйверы основного ПК не изменялись.

## Исправление восстановления

В upstream `WindowsNetworkWatcher::initialize` создавал power-window только после
успешных WlanOpenHandle и WlanRegisterNotification. Ошибка WLAN прерывала подписку
на пробуждение. Старое окно также не сохранялось для уничтожения.

Теперь power callback регистрируется независимо от Wi-Fi, без окна. Callback
ставит обработку в Qt-поток объекта; учитывается PBT_APMRESUMEAUTOMATIC, повторный
PBT_APMRESUMESUSPEND не создаёт второе восстановление. Регистрация снимается при
уничтожении watcher. Повторный initialize не создаёт дополнительные регистрации;
при неудачной WLAN-подписке освобождается уже открытый WLAN handle.

Нативные тесты используют подставленные API: недоступная WLAN-служба, отказ
регистрации WLAN, переход из callback-потока в Qt, игнорирование повторного события,
остановка наблюдения и удаление объекта до доставки очереди. 5 PASS с init/cleanup,
0 FAIL; `.research/windows-wakeup-native-2026-10-18.txt`.
Это не физический сон/гибернация VM или основного ПК.

Основание API: [PowerRegisterSuspendResumeNotification](https://learn.microsoft.com/windows/win32/api/powerbase/nf-powerbase-powerregistersuspendresumenotification),
[Windows system wake-up events](https://learn.microsoft.com/windows/win32/power/system-wake-up-events).

В `VpnConnection::createProtocolConnections` устранено накопление одинаковых
подписок IPC на networkChanged/wakeup при каждом новом протоколе. Для соединения
с постоянной replica теперь используется QueuedConnection | UniqueConnection.
Регрессионный тест после четырёх подготовок соединения:

- до исправления: одно wakeup → четыре attemptRecoveryRequested, ожидаемый FAIL;
- после исправления: одно wakeup → один запрос, следующее networkChanged → ещё один.

Доказательства: `.research/wakeup-duplicate-before-2026-10-18.txt` и
`.research/wakeup-duplicate-after-2026-10-18.txt`.
Связанные CTest HopSelectionSession / HopWindowsNetworkWatcher / HopXrayLifecycle:
3/3 PASS, 7.52с. Использована подставная локальная служба, реальный VPN хоста не запускался.

## Интерфейс

В рамках исходного требования понятного UI исправлена утечка внутренних
имён контейнеров в каталог и выбранный протокол: вместо `amnezia-xray` и `amnezia-awg`
показываются пользовательские названия. Для VLESS с указанным Reality —
`VLESS / Reality`; остальные контейнеры используют существующий словарь названий.
Это не изменение политики или формата сохранённых конфигураций.
HopLocationCatalog / HopNativeDashboard: 2/2 PASS, 4.02с.

## Ресурсы в покое

Отдельные 30-секундные интервалы выполнялись внутри настоящего обычного GUI,
с работающим Qt event loop, без тестового polling в клиенте. Внешний VM-probe
снимал счётчики CPU и памяти только EXE из точного каталога Hop раз в секунду.
Учитывались GUI, SCM-служба, собственный AWG-worker, tun2socks и замеченные helper.
Тестовый агент, тестовый probe и стенд в показатели клиента не входят.
Параметры VM сверены: 1vCPU, 6144МиБ RAM.

| Сценарий | CPU, % одного ядра | Пик суммы private, МиБ | Пик суммы working set, МиБ | Job |
| --- | ---: | ---: | ---: | --- |
| BlancVPN, отключено, до подключения | 0.104 | 159.82 | 176.31 | 4f0ee90a4d18400289a4495e65f2add1 |
| BlancVPN, VPN подключён | 0.624 | 238.14 | 275.13 | 8c15896eba2a43c198dd7cfe44fcb556 |
| AWG-стенд, отключено, до подключения | 0.104 | 99.52 | 156.50 | ef38ca0ede2540699383de24fe4e6f8a |
| AWG-стенд, VPN подключён | 5.152 | 319.97 | 265.57 | 184df788d74f4252a344dec4f1e4dbc8 |

Все четыре job: exit0/recovery0, 31 выборка, complete=true/issues=[].
Каждый также прошёл GUI-импорт/подключение/Direct/live Block/переподключение/
штатное отключение с guard0 и очищенным UI, QML warnings0.

Private — committed private memory, а не только физическая RAM. Сумма working sets
может повторно учитывать общие DLL-страницы. Процессы короче интервала выборки
могут быть пропущены. Первая/прогретая служба и сохранённые Go runtime allocations
дают различающиеся disconnected-показатели; таблица не доказывает утечку памяти.
Это короткие интервалы VM, не длительный soak/battery/физический benchmark.

AWG-стенд использует настоящий зашифрованный AWG3.1 и Go netstack на loopback хоста,
без системного TUN хоста. Шифрование/CPS/padding и PersistentKeepalive5с входят
в профиль. Получение 4МиБ проверено SHA256: 4.88Мбит/с (6875мс) и 3.95Мбит/с
(8492мс) в этих прогонах. Это производительность стенда и VM, не предел ISP/сервера.
Fixture udpReceived/udpSent — счётчики байтов, не количества пакетов.

## Итоговая сборка клиента

Hop.exe: `d574da1b15ae9618c3dc1d2042ecafa743fcbf036fbcaf389a99e1c2982cf4e2`.
Hop-service.exe: `8b6e7fa3eae73857bec9de37dd3131f4a0ff5d096e0b8f1792f30dc9ade1f306`.
RuntimeStage `852e429abbb441f6aa4e7b84d9ce344f`, exit0/recovery0, проверены
хеши обоих файлов после замены и работа собственной SCM, guard0.

Обычный GUI BlancVPN: `5c3f56fe8fe14baeb24a3e5e4cf450c6`, exit0/recovery0.
Импорт, VPN/Direct, live Block, переподключение и отключение1518мс прошли;
guard0, UI clear, ACL журнала denied обычному пользователю, QML warnings0.
AWG с реальным DOWN/UP гостевого uplink: `cd7a46efe22247959732756ad9b7101f`,
exit0/recovery0. Отключение сети4с наблюдалось и было восстановлено, GUI подтвердил
networkRecovered. Затем прошли live Block/Direct/переподключение/отключение2775мс,
guard0/UI clear/QML0. В старом MSI-HASH этого архива указан только прежний пакет
установки; текущий runtime подтверждён SHA обоих EXE в BUILD-MANIFEST и RuntimeStage.
Дальнейшие AWG GUI-пробы сохраняют RUNTIME-HASH, без приписывания кандидата старому MSI.

Финальные девять Hop CTest пересобраны и прошли9/9, 19.33с:
`.research/client-final-native-ctest-2026-10-18.txt`.
Замеры idle выше сделаны до последнего устранения повторных подписок; после него
проверен функциональный сценарий. Они не выдаются за повторный benchmark финального EXE.

Пользователь 18 октября перенёс обход белых списков (пункт 9 исходного ТЗ) в
отдельный отложенный этап. Он не блокирует завершение текущей Windows-версии.
Параметры Reality/AWG поддерживаются; работа на реальной ограниченной сети
пока не подтверждена. Текущая VM через Amnezia хоста такую сеть не воспроизводит.
Новые обязательные этапы из собственных инженерных предложений не вводятся.
