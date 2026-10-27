## Обновление 18 октября

Актуальное продолжение: `docs/hop/continuation-2026-10-18.md`.
Подготовлен новый MSI0.1.1 (ещё не принят в VM) и сценарии upgrade/rollback/downgrade;
два теста аварии сохранения подписки прошли. VM-агент устарел, ожидается ручной запуск.
Ниже — история принятого MSI90ee от16октября.

## Последнее состояние 16 октября, около 22:43 МСК — GUI нового MSI принят

Приоритет: пользователь исключил постоянный boot/always-on Kill Switch и попросил
согласовывать несогласованные функции. Не возобновлять boot-расширение/reset.
Основной ПК/Amnezia/его сеть не менять, Windows only, без коммитов/публикации.

Текущий MSI90eeb42973e23a69c2a16e3eb1a3bec0d20d7cbcc93178d174358a774cbaabad
установлен в VM и прошёл GUI AWG/VLESS + network recovery. build/windows-test соответствует
ему; старый714... сохранён .research/accepted-builds/714aceed. Новых изменений продуктового
кода после успешных native/VM тестов нет; позднее менялись только отчёты.
AWG GUI2b00bcb8932f446ebd7bfa42d756d53c, VLESS GUIc1d1865b71094b6f9a0c053e98e3e7a4,
networkRecovery6aac6b21812e48b5a0aa6a61ce49853c: все exit0/recovery0, guard0/UIclear/QML0.
Candidate AWG проверен обычным пользователем до подключения1022ms и deferred после.
Новый отчёт docs/hop/windows-awg-health-validation-2026-10-16.md, HOP.md/roadmap обновлены.
8nativeCTest PASS19.05s, Go4tests PASS; неизменённые проверки повторно не запускать.
VM agent свежий/ready seq22 около22:41, ручной20:58 до00:58 17октября, не продлевать.
Последние лимиты used94% пятичасового окна. Кредит сброса не тратить, ПК НЕ выключать.
Автоматизация hop PAUSED, однократный wake использован, не включать заново без запроса.
Остались actual Windows границы: IPv6 tunnel/Direct IPv6, длительный общий idle,
upgrade/rollback, libagw license review для публикации. Отрицательную boot-диагностику
сохранить как историческое ограничение вне согласованного объёма, не считать blocker.
Невыданные API-профили не создавать ради health; это расходует лимит подписки.
## Новейшее состояние 16 октября, около 22:36 МСК

Пользователь исключил boot/always-on расширение; НЕ возвращаться к нему.
Ограниченный ручной агент20:58 по-прежнему работает, не продлевать.
Первоначальный MSI714... сохранён в .research/accepted-builds/714aceed с исходными
SOURCE/PAYLOAD-MANIFEST. Старый пакет штатно удалён из VM:
`40c2ff4a67f044bd94e631042d8e9f84`, exit0/recovery0, guard12 до удаления ->0,
свои файлы/SCM удалены. Основной ПК не менялся.

НОВЫЙ MSI уже установлен в VM, но полный GUI ещё проверяется:
SHA25690eeb42973e23a69c2a16e3eb1a3bec0d20d7cbcc93178d174358a774cbaabad,
build/installer/Hop-0.1.0-windows-x64.msi, shared build/windows-test/Hop-Setup.msi.
204файла,99,155,261байт; VM агента/тестовые EXE в MSI нет, Go notices включены.
InstallerInstall `0b64f116328c4b18a69d5576b4e5304e`, exit0/recovery0,
MSI0/ownedSCM auto/path/GUI hash совпали. Весь новый stage теперь соответствует этому MSI.

AWG candidate probe ИСПРАВЛЕН и прошёл отдельную сеть:
`8d8af9c7572d4512b79ad9460e833296`, exit0/recovery0, encrypted DNS reachable1021ms,
при active flag deferred, при /pause-dns unreachable, own adapters unchanged.
Исправлено приведение IPv4-mapped адресов Resolver через Unmap для Windows bind.
У «холодного» туннеля первый DNS-обмен терялся; теперь не более3 одинаковых запросов
с проверкой ID/вопроса/QR/rcode, общий deadline10s. Молчание DNS не выдаётся за успех.
Первоначальные c532...,1454...,412a...,77f...,1662...,6575... negative сохранены.
Numeric Error/IpcGet diagnostics не выводят raw keys/endpoint, stdout<=1024.
Не расширялись WFP-разрешения или IPC trust; новый helper работает без системного TUN.

Все8nativeHopCTest после новой интеграции PASS19.05s:
.research/awg-health-all-native-ctest.txt. Go4 test functions PASS0.5–0.7s,
включая неподдерживаемые/опасно большие параметры и поддельные DNS-ответы.
Resource sampler расширен на все собственные helper EXE, прежний debug-handle bug исправлен.
Ресурсный отчёт старого714...: docs/hop/windows-resource-validation-2026-10-16.md.

Текущая exec session6640 выполняет test-awg-fixture.ps1 -InstalledGui:
.research/awg-health-installed-gui-result.txt. СЛЕДУЮЩИМ проверить результат.
Новый обычный GUI тест дополнительно требует AWG candidate reachable до подключения
и deferred при active VPN. Дальше выполнить InstallerGuiVpn с BlancVPN на том же MSI.
Не повторять неизменившиеся nativeтесты после их успеха; только необходимые VM проверки.
Последний лимит около22:34: used90% пятичасового окна. Сохранить checkpoint и остановиться
около95%, кредит сброса не использовать. ПК НЕ выключать, автоматику hop не включать
заново без нового запроса (однократный wake уже использован/PAUSED).
## Приоритетное продолжение 16 октября, около 22:03 МСК

ВАЖНО: пользователь явно исключил постоянный Kill Switch до запуска Hop и после
полного reboot. Ответы: «Да, это уже было лишнее», «если Hop не запущен, то Kill Switch -
не его проблема», «На будущее - уточняй надобность несогласованного функционала».
Зафиксировано в requirements.md/HOP.md/roadmap.md. Не продолжать boot-расширение,
не выполнять дополнительные reset/RegFlush/новые журналы восстановления ради него.
Сохранить отрицательные результаты как ограничение, не выдавать их за успех.
Согласованный Kill Switch — работающая сессия, обрыв/авария ядра/службы/переключение,
Direct сохраняется; явное отключение и штатное закрытие снимают защиту.
Новые функции/усиленные требования сначала согласовывать. Работать дальше автономно
в пределах исходного ТЗ, Windows only. Основной ПК/Amnezia/его сеть не изменять.

Текущий установленный и ранее проверенный MSI остаётся714aceed40b84a450c106ea9d9d956e8029da510eae9c0243f09993d9baeb33a.
НО build/windows-test уже содержит НОВЫЕ НЕПРИНЯТЫЕ Hop.exe и hop_awg_probe.exe,
а Hop-Setup.msi в stage ещё старый714... . Не считать stage проверенным пакетом!
Перед сборкой/заменой MSI сначала InstallerRemove со старым сохранённым714..., затем
build-installer, копия нового MSI в shared stage, InstallerInstall и GUI AWG/VLESS.
Не коммитить/публиковать. Агент ручной20:58, срок до примерно00:58 17октября, не продлевать.

Полные ресурсные прогоны старого MSI714... прошли:
- AWG `aba2c94048b544ce927887fcbdbfb9d1`, exit0/recovery0: complete=true,
  peaks simultaneous working/private269.67/315.96MiB, whole scenario CPU11.87% одного
  CPU за67.66с. Все Hop.exe, SCM/tunneldaemon Hop-service.exe и tun2socks учтены.
  GUI/live edit/reconnect/Block/Direct/disconnect guard0,4MiB SHA verified5.606Mbps.
- VLESS `bb74311dbfd54493b42b908d60a0f0e1`, exit0/recovery0: complete=true,
  working/private190.95/223.93MiB, whole scenario CPU6.79% одногоCPU за28.08с;
  весь GUI-сценарий/ACL/QML0/disconnect guard0/UIclear.
  Это пики с интервалом1с и средний CPU сценария со startup/reconnect, не общий idle.
- Предшествующий `0f228135f3ff475c97723fb78856e839` sampler complete=true, но
  единичный DirectBeforeEdit=false; отрицательный результат сохранён, повтор bb743... прошёл.
- Найдена и исправлена ошибка test-only debug-loop: нельзя рано закрывать hThread/
  hProcess из DebugEvent — ОС закрывает их после EXIT/ContinueDebugEvent и могла
  закрыть переиспользованный handle sampler. До исправления process times error6,
  complete=false. Документация Microsoft WaitForDebugEvent подтверждает lifetime.

Теперь дорабатывается исходный пункт7: проверка ДОСТУПНОСТИ AWG-кандидатов.
Добавлены hop/native/awg-probe (Go7MB, amneziawg-go/v3 v3.1.20260814, NetTUN в памяти,
зашифрованный DNS, профиль stdin, никакого системного TUN/SCM/routes/WFP), CMake/install/
stage hash. EndpointHealthController запускает AWG последовательно, не более2проверок
суммарно; cancels AWG до основной VPN-сессии. Причина: одинаковый ключ может перенести
endpoint сервера и оборвать активный VPN. При активной сессии AWG deferred; активная
LocationSession продолжает штатный DNS health. API-профили ради проверок не выпускаются.
Go-парсер ограничивает размеры JC/Jmax/S*, CPS и padding, маршрутизирует только DNS/32.
Поддерживаются основные параметры3.1/HeaderProtectionKey; непроверяемые профили дают
unsupported, не ложный offline. UI «После VPN» и пояснение, задержка не throughput.

Нативный HopEndpointHealth CTest с новой регрессией serial/cancel/deferred/retry PASS5.13с
(вне AppContainer, фиктивные процессы, без хостовой сети). Четыре Go test functions
(profile/invalid+resourcebounds/no-secret-echo/DNSnonce) PASS0.5–0.7с, без сетевых запросов.
Полные8native suites ещё не перезапускались после изменения общего test_common.
Сборка Hop/probe прошла. AppContainer reconfigure не видит часть Conan-кэша;
та же сборка вне sandbox прошла из кэша. Go build получил -buildvcs=false, иначе git
submodule status denied; -trimpath/CGO_ENABLED=0/GOOSwindows/amd64.

Сетевая AWG-кандидат проверка ЕЩЁ НЕ ПРИНЯТА:
`c532d2c7b3fa47869bea575f781f8c19`, `14547faa596c4621bc6b89ec10fc29cd`: exit7/recovery0,
state unreachable, helper stage reply; fixture UDP/DNS counters0, handshake0 неизвестен.
Добавлен безопасный числовой logger/IpcGet diagnostics (без raw keys/endpoint).
Текущая команда exec12068 выполняет test-awg-fixture.ps1 -EndpointHealth,
результат .research/awg-endpoint-transport-result.txt. ПРОВЕРИТЬ следующим шагом.
Go helper default Windows bind может отличаться от loopback fixture bind; причина
ещё не установлена, не расширять WFP-разрешения спекулятивно.

VM probe awgEndpointVmProbe.h сначала проверяет положительный зашифрованный DNS,
затем deferred flag, /pause-dns fixture -> timeout/unreachable, unchanged own adapters.
Секреты только RSA/AES обмен/stdin; ограничения агента не менялись. Новый UI AWG-тест
перед подключением требует candidate probe, затем deferred while connected;
нужен новый MSI после исправления endpoint probe. Фикстура хоста ТОЛЬКО loopback
(без хостового TUN/routes), запускать вне AppContainer, иначе NATVM не достучится.
## Продолжение 16 октября, около 21:24 МСК

Ручной агент после второго reset запущен около 20:58 МСК, четырёхчасовой срок до
примерно 00:58 МСК 17 октября. Самостоятельно не продлевать. Первый job после запуска:
`09e60a7b9edd486eb4c137812bfcd0ce`, exit14/recovery0. До initialRecovery найдено
0 собственных фильтров, включая disabled; reboot=true, provider persistent flags1,
serviceName Hop-service, SCM running4/auto2/errors0. Выдержка 30 секунд не помогла.
Состояние boot-guard-state.json теперь phase=observed, retained=false. Защита при
загрузке НЕ принята. Продуктовые WFP-фильтры/служба/MSI не менялись.

`13334b765d5f42cda98e3e06c77cbd98`, exit0/recovery0: все 12 применённых фильтров
найдены как значения в HKLM SYSTEM/BFE/Policy/Persistent/Filter; stop/start своей SCM
сохранил их GUID, Direct200/защищаемыйQt99. Читались только метаданные, не содержимое
реестра. ACL/аудит/реестр не менялись. `1741a234a9bf4692a8f60a5f2e1692d2` и
`7ebaf6010183448090f6ca96f1720e51`: Current/Default/LastKnownGood ControlSet=1,
автозапуска Hop GUI не найдено, событий Security5441/5447 нет. Зависимость BFE mpssvc
не принимает STOP; принудительный перезапуск BFE и остановка Defender не выполнялись.

Пользователь спросил, как должен работать Kill Switch. Объяснено: до подключения,
при обрыве/аварии/переключении блокировать назначенный VPN-трафик; Direct сохранять;
явное отключение снимает защиту. Честно отмечено, что строгий режим постоянной защиты
после полного reboot был расширением проверки сверх исходного согласованного обрыва.
Это инженерная рекомендация; отдельного решения пользователя о постоянном режиме
пока нет. Не выдавать отрицательную boot-проверку за пройденную и не менять семантику
защиты без учёта дальнейшего ответа пользователя.

Независимо добавлен test-only sampler точных путей установленного Hop.exe,
Hop-service.exe (включая tunneldaemon) и tun2socks.exe, с удержанием handles и пиками
одновременной памяти. Первые VLESS прогоны `e825d913ff4c4778add097f7a84ca5ef` и
`862f90b0b5b04796a2bc921ab1dcc2ac` прошли GUI/guard0, но sampler complete=false.
Их цифры не считать полной ресурсной приёмкой. Исправлен SYNCHRONIZE и добавлены
точные коды ошибок измерителя; очередной VLESS прогон выполняется. Текущий MSI всё ещё
714aceed40b84a450c106ea9d9d956e8029da510eae9c0243f09993d9baeb33a.
# Актуальное продолжение 16 октября, около 19:34 МСК

## Второй диагностический reset выполнен, около20:54МСК

Первый post-boot `85fe54624b3f4ed3be6d17e16458b8d2` НЕ прошёл: reboot=true,
действующих исходных фильтров0 до initialRecovery, exit14/recovery0. Boot-приёмка открыта.
Диагностика `ea09cd0521884a26adeb1bb57a6ec4e5`: provider persistent flags1,
serviceName Hop-service; SCMrunning4/autostart2/errors0. Два дефекта test helper исправлены:
QFile state.close() перед QSaveFile (иначе atomic replace не проходил); INCLUDE_DISABLED
перечисляется по конкретным ALE_AUTH_CONNECT_V4/V6 слоям (нулевой layerKey ошибочен).
Инвентаризация `7e2665de33784961be84a5167bbd2f50` дала0включаяdisabled, но уже после
первой cleanup — это не замена исходному наблюдению. Живую политику продукта не меняли.
`9098de6185f94bb7a33e58d3280f2ea3`, exit0/recovery0: stop/start только проверенной
установленной SCM-службы сохранил все GUID enabled-фильтров, Direct200/BlockQt99.

В рамках уже данного пользователем разрешения на аварийный reset только Windows10 VM
выполнен второй диагностический reset, после30секунд выдержки guard. Это не исправление!
Job `104ad13e9fcf46c0841492725e4c4f89`, nonce `2ba58c2b038d499eb2c2f82b8ce52200`,
12persistent/6block/disabled0/dynamic0/settledMs30000. Архив с boot-prepared.json,
BUILD-MANIFEST/MSI-HASH сохранён. MSI714aceed... прежний, менялся только test probe.
VBoxManage reset0; VMrunning, RunLevel3, обе transient-папки сохранились.
Пользователю отправлен запрос ручного агента после второго reset; ответ ожидается.
Agent/probe погибли при reset ожидаемо. Новый ручной агент нужен для ПЕРВОГО задания
BootInspect; другие VM jobs до этого не запускать. Самостоятельно агент не запускать.
Ожидается post-boot наблюдение (пока не пройдено). HOP.md/network report обновлены.

## Перезапуск VM выполнен, около20:38МСК

Пользователь явно разрешил аварийно перезапустить только Windows10 VM.
Fresh ready агента и точный UUID проверены, MSI прежний714aceed... .
Новая подготовка boot guard: job `8c303c7550e44249a34d58772801f137`,
nonce `83703b0edca0447591ec9d90d4975c85`, 12persistent/6block/disabled0/dynamic0.
Снимок сохранён в `.research/vm-runs/8c303c7550e44249a34d58772801f137/boot-prepared.json`
с BUILD-MANIFEST и MSI-HASH. Затем VBoxManage controlvm точного UUID reset завершён0.
Основной ПК не перезапускался, Amnezia/сеть хоста не менялись.
boot-guard-state.json остаётся phaseprepared, nonce совпадает, uptimeMs4412609.
GuestAdditions поднялись заново, RunLevel3; обе transient-папки остались подключены.
Пользователю отправлен запрос ручного нового агента после reset; ответ пока ожидается.
Проверка защиты после загрузки ещё НЕ выполнена. Старый агент и probe погибли при reset,
отсутствие финального JSON у подготовительного job ожидаемо. Самостоятельно агент не запускать.
После загрузки запросить ручной запуск Hop-VM-Agent, затем ПЕРВЫМ заданием BootInspect;
никакие другие jobs до него не запускать (initialRecovery удаляет guard после наблюдения).
Первый новый probe до initialRecovery снимет GUID/ENABLED/uptime и Direct/Block,
сохранит phaseobserved. После этого обычная очистка. Не объявлять boot-приёмку по reset0.

## Последнее состояние, около20:24МСК

Продуктовый MSI остаётся `714aceed40b84a450c106ea9d9d956e8029da510eae9c0243f09993d9baeb33a`,
установлен в VM. Строгий GUI AWG/VLESS и восстановление сети прошли (ID ниже).
VLESS внешний UDP9953 прошёл: `524a38f48aeb4f8dbcb307849acaefd1`, exit0/recovery0.
В том же прогоне DNS53/853 capture0/0, IPv6 link-local0, TCP DNS denied10013,
VPN/Direct/Block/DNShealth положительные, guard0 после отключения.

Полные публичные DAT: `a691a8c23258465c98ab34d29c98d7d0`, exit0/recovery0.
GeoIP351134байт/2категории/23001запись; GeoSite81911байт/81категория/3981запись.
Все категории скомпилированы и прошли snapshot round trip, кэш перечитан с тем же SHA.
Исправлен только тестовый выбор helper: при живой установленной SCM-службе используется
проверенный по пути/хешу установленный Hop.exe, поскольку WFP control leases привязаны
к нему. Не расширять разрешения ради тестовой копии. Прежний negative4e7817... сохранён.
Отчёт: docs/hop/windows-network-validation-2026-10-16.md.

Подготовлен bootGuardVmProbe.h и режимы HopBootPrepareTest/BootInspect.
Продуктовый MSI/служба не менялись. Главный probe снимает наблюдение prepared-фильтров
ДО обычного initialRecovery: сравнивает GUID/ENABLED/uptime, Direct200 и защищаемый
путь blocked. Затем обычная очистка. Подготовка включает только прежний guard через IPC,
пишет несекретный boot-guard-state.json, ожидает60сек и отменяется при отсутствии reset.
Без перезапуска dry job `45eed50223b64c6c81606b4479c7d1ca`: ожидаемый exit14/recovery0,
Direct200/защищаемыйQt99, 12persistent/6block/disabled0/dynamic0, phaseexpired,
агент снова ready. Это проверка ограниченного ожидания, а не приёмка загрузки Windows.
Архив сохранён с манифестом/MSI-хешем. Сборка probe и PowerShell parse прошли.

Пользователю отправлен async-запрос РАЗРЕШЕНИЯ аварийного reset только VM; ответа пока нет.
Причина отдельного подтверждения — возможная потеря несохранённых данных внутри VM,
не отказ автоматической проверки. План: docs/hop/boot-guard-validation-plan.md.
До разрешения НЕ перезапускать VM. Если разрешено: fresh ready → новый send-vm-test-key
с [Interface]/# HopBootPrepareTest → дождаться свежего phaseprepared и сохранить job/nonce,
затем reset точного VirtualBox UUID b79c0594-8e7d-42f6-85b6-3b9784efdb1c в пределах60сек.
После загрузки попросить ручной новый агент; самостоятельно не продлевать/не запускать.
После ручного запуска сразу invoke-vm-scenario -Scenario BootInspect, до любых иных jobs:
первый probe наблюдает guard до очистки. Чтение boot-state обязано видеть uptime меньше
старого и исходные GUID. Если разрешения нет — не запускать подготовку с reset.
Пока не создано задач планировщика/новых служб/автопродления агента. Хост не затрагивается.
Лимит проверен около20:14:10% пятичасового окна, reset credit не использовался.

## Обновление после запуска агента около19:53МСК

Обе transient-папки VirtualBox восстановлены open-windows-vm-test.ps1; пользователь
вручную запустил новый4чагент. Fresh ready hostmtime/guesttime подтверждены.
Самостоятельно не продлевать, ориентировочный предел23:53МСК16окт.
СтарыйMSI удалён9c482ec0996e45cba2355c08e90a0341, guard12→0/files+SCMremoved.
build/windows-test теперь содержит исправленный714aceed...MSI и новыйstage,
InstallerInstall cb9a2cd7703445bb845be1ed1e7a5205 exit0/recovery0.
Строгий AWG GUI06528c878df24d0cab82c443453bbc26 passed0/0, disconnect4175мс,
guard0/UIclear/sessionfalse/progressfalse/finallytrue/4MiBSHAverified5.60Mbps.
VLESS GUI4fe4d10ac96546be93bc1e470b8d6bb7 passed0/0, disconnect2437мс, остальные
условиятеже, реальнаяBlancVPN. AWG NetworkRecovery d6e8aa5b100b4b06a3183ac5ca62a1e8
passed0/0, guestuplinkDOWN4s→UP/VPNoK/DirectOK/disconnect3743мс/guard0/UIclear.
ПервыйAWG94f20e89...fixtureвAppContainerполучил0пакетов/connectfailed; отрицательный
сохранён. ТотжеMSI соfixtureвнеAppContainerпрошёл; fixtureтолькоloopbackбезhostVPN.
HOP.md/roadmap/GUIreport обновлены: строгийGUIdisconnect теперьпринят вVM.

Дальше: внешнийUDP VLESS, отдельно отDNSport53перехвата. ВvlessPolicyVmProbe.h
добавленUDPquery9.9.9.9:9953 сновымID/случайнымQNAME, проверкойremote/port/ID/question/
rcode; bindIPv4собственногоHop-TUN иroutealiasпроверяется, нетTCPfallback.
Quad9официальноподдерживает9953: https://docs.quad9.net/services/ . Это толькоtesthelper,
продуктовыйMSIне меняется. Компиляцияhop_windows_vpn_probe session20306,
лог.research/vless-udp-probe-build.log. Послеуспеха обновитьstage/manifest не меняя
Hop-Setup.msi изапуститьVlessPolicy. НельзяобновлятьstageприактивномVMjob.

## История до запуска агента

Пользователь разрешил продолжить после паузы. Лимит проверен:42% пятичасового окна,
кредит сброса не использован. Ограничения хоста/Windows-only/без публикации прежние.
VM agent-status stopped, последнее обновление2026-10-15T22:54:11Z; пользователь получил
запрос ручного запуска агента для проверки исправления. Самостоятельно не запускать.

Прочитан strict-gui-awg-result:392d6fe9878f40d6866fa75f8c65077f exit7/recovery0,
connectSignalObserved=true/sessionRunning=false/uiProgress=false/guard13 после20.67с.
Причина воспроизведена локально: после parent clear деструктор WireguardProtocol
с preserveProtection=true снова применял guard. Регрессия до исправления упала:
.research/awg-destructor-before-fix-detail.txt. Добавлен private constructor с fake
ControllerImpl для теста без настоящего pipe/SCM/сетевых изменений. Деструктор Hop
при preserveProtection && !started больше не повторяет stopAfterFailure/applyGuard.
Незавершённый транспорт по-прежнему останавливается, авария не снимает защиту.
Проверены оба варианта ACK/нет ACK. Все8native suites passed13.87сек:
.research/awg-destructor-fixed-tests-unsandboxed.log. Сначала4набора упали внутри
AppContainer на записи защищённых тестовых настроек; вне sandbox все прошли,
продуктовый код ради этого не менялся. СборкаHop/tests успешна.

Новый stage build/windows-test-next, Hop.exe SHA3dc51bc6618c800b944cfbae000d388b675b66d9e6669b4dfc46bab09d763fe0.
Installer build session31366 завершён успешно, лог .research/awg-destructor-fixed-msi.log.
НовыйMSI SHA714aceed40b84a450c106ea9d9d956e8029da510eae9c0243f09993d9baeb33a,
скопирован только вbuild/windows-test-next/Hop-Setup.msi. Готов к замене после старогоuninstall.
Старый stage build/windows-test НЕ заменён, там CAD33...Hop-Setup.msi ещё нужен
для InstallerRemove установленного старого ProductCode. Сначала свежий ручной агент,
InstallerRemove старогоMSI с архивированием результата, потом заменить stage новым,
скопировать новыйMSI вHop-Setup.msi, InstallerInstall, InstalledGuiAWG иVLESS.
Системная приёмка исправления ещё НЕ выполнена. SBOM/CI/лицензионные задачи сейчас
не развивать вместо закрытия основного GUI-сценария. Отчёт GUI/HOP.md/roadmap
уточнены: строгий disconnect пока открыт, прежние положительные результаты исторические.

# Исторический checkpoint 16 октября, около 01:50 МСК

Windows only. No commits/publication. No subagents. Не менять Amnezia/сеть/службы/драйверы хоста.
Привилегированные тесты только Windows10 VirtualBox через bounded Hop-VM-Agent.
PC не выключать; hop automation PAUSED, одно пробуждение использовано, не пересоздавать.
Reset credit не расходовать. Последний лимит94%, reset Unix1792114244.
Пользователь вручную перезапустил агент около00:41МСК16окт, срок4ч до~04:41МСК.
Не продлевать/не запускать самим. Проверять hostmtime agent-status<=15sec, guest UTC diff<=120sec.

## Срочное продолжение

Windows НЕ завершена. GUI после добавления строгого ожидания отключения ещё не принят.
Сейчас build/windows-test готовится из новой диагностической сборки; процесс MSI session35310,
functions exec cell405 ждёт его и запускает InstallerInstall + test-awg-fixture -InstalledGui.
Выход cell405 сообщает exec session проверки; затем читать .research/strict-gui-awg-result.txt.
Скрипт проверки архивирует id/результат/manifest/MSI-HASH в .research/vm-runs.
Не менять stage во время задания. MSI ProductCode меняется: удалить текущий через InstallerRemove
до замены Hop-Setup.msi. Последний установленный A842... удалён b8844235c8bd40e387fb82e3137bb653,
exit0/recovery0/files+SCM removed/WFP0; новая установка выполняется только после сборки.

Ранее access violation disconnectFromVpn исправлен (strong snapshot, reentry guard, timer parent/singleShot),
3 native regressions passed. Затем реальные GUI/VLESS и AWG + ACL были успешны, см. GUI report.
При более строгой проверке обнаружено: после кнопки disconnect иногда ещё progress=true спустя10сек;
finish повторно отключал и получал disconnected=true. 9597ff692f6145d1b6868ab140baf36d exit7,
connect/Block/Direct/reconnect/4MiB SHA всё успешно, disconnectButton=false, finish cleanup=true.
43f9cbab119f47408205591a78d9d923 тоже exit7: GUIexit0, но finish disconnected=false.
Не считать принятыми. Новый harness ждёт !session.running + !core.isConnected + !ui.isConnectionInProgress +
guard0 до20сек, записывает connectSignalObserved/manualDisconnectMs/uiProgress/sessionRunning/guard.
finish не отправляет повторную команду после подтверждённого успеха. Если снова failure — разбирать
доставку clicked vs долгий cleanup, не ослаблять ACK. IpcClient thread_local, общего межпоточного replica нет.
ConnectionController::isConnected читает VpnConnection enum напрямую, возможный будущий аудит race.

## Подтверждено за эту сессию

GUI MSI79849...: 42efb444f6bb4a0ea1e95d101ad63503 +32767810... VLESS ordinary GUI/live edit,
Direct/Block/disconnect0/ACLreadDenied true/QML0. MSI b95aa...: AWG GUI4f70a794a25443bca1231998454b211c,
VLESS GUIcb4d38f748fc44fdb3fe1b534c344fba, оба0/0. docs/windows-gui-validation-2026-10-16.md.
GUI3сек RAM ранее115–162MiB working/85–114private, CPU0–1.56% одного CPU; не полный бюджет.
Main SCM service по точномуimage: peak33–36MiB working/62–64private, avg1–1.6% CPU за весь сценарий.
Native AWG child и tun2socks в этот budget НЕ включены.
4MiB fixture SHA verified:25edbb17...5.86Mbps,43f9...5.27,9597...5.74. Один15f3... не уложился16сек,
получил2.5MiB: отрицательный сохранён. Это VM/local peer throughput, не провайдер/физическая Windows.
Peer UDP buffers теперь7MiB как conn.StdNetBind, только socketoptions loopback, не сеть хоста.

46085cb96e38416dbf4247fc8d01036b: гостевой физический адаптер фактически DOWN4сек→UP,
LUID совпал, VPN+Direct после восстановления, live edit/reconnect/cleanup/QML0.0/0.
Предыдущий71562... имел преждевременный log restored=0 до операции; новый test подтвердил состояния.
Сон/BFE/подготовленная перезагрузка ещё не проверялись. Не вызывать reboot хоста.

Domain/DAT regexp теперь проверяет hop_endpoint_probe --validate-domain-patterns: реальный Xray
core.New/router, без запуска/входов/сети. Отдельный процесс избегает второго Go runtime в GUI/agw.
Ошибки/отсутствиеhelper/timeout сохраняют прошлую политику. DAT bad-regex не заменяет кэш.
Geosite domain/full проходят те же ограничения что ручные/снимок. Все8 suites прошли8.84сек
(.research/packet-ui-final-tests.log); последний диагностический header после этого только compiled.
ConnectionUiController refresh guard сразу приstateChanged, pending refresh coalesced, старыйwatcher
не очищает новый. GUI тест дополнительно требует protectionUiCleared; финальная проверка pending.
Hop LocalSocketController больше не выводит pubkey пира в handshake debug.

## DNS и ограниченная IPv6 packet acceptance

hop/tools/physicalDnsVmProbe.h использует встроенный pktmon в VM: отказывается при чужом capture/filter,
сам находит hardwarecomponent по точномуifIndex, фильтры только1.1.1.1 UDP53/853 и ff02::1 UDP11944.
ETL/PCAP временные, вывод только счетчиков, ownfilters/ownsession удаляются. EPB bounds/BOM проверяются.
UDP connect/send возвращал0 даже при WFP-drop: WSA0 НЕ означает утечку. TCP53/853 WSA10013.
Положительный контрольe575... и1d16ee1...: физическийNICвидит53/853 1/1 безguard.
VLESSa1095085... иAWG5256847...: приguard0/0, реальныеDNS/HTTPVPNpositive,Direct/Block/cleanup0.
Новая IPv6linklocalпроба: fe9a14fdb3b543d79f9b838b9b84348e безguard DNS1/1,IPv6packet1;
VLESS9f214bdda8ce4724853ba10e7a6ec2ba иAWG (id в .research/ipv6-captured-awg-result.txt)
приguard DNS0/0 иIPv6packet0, реальныеVPNpositive,Direct/Block/cleanup0. Всеexit0/recovery0.
Это не глобальная IPv6 connectivity и не доказательство всех возможных DNS/DoH/boot путей.
Физический IPv6 блокируется; полный IPv6 tunnel/Direct ещё не принят.

## Сборка/инвентаризация/лицензии

Новые исходные build-*.ps1/enter-build-env.ps1 были скрыты upstream ignore*build-*; исключения добавлены.
Скрипты теперь имеют optional HOP_VCVARS/HOP_QT_ROOT/HOP_PYTHON_SCRIPTS/HOP_GO_EXE/HOP_ZIG_EXE
дляCI, локальные defaults сохранены. .github/workflows/hop-windows.yml manual-only Windows2022,
Qt6.10.1/MSVC/Go1.27.2/Zig0.17.0 SHA b5663f...; native tests/stage, без VPN/публикации.
YAML/PowerShell parse passed, реальный GitHubCI НЕ запускался. В .gitignore добавлен whitelist скриптов.
source_fingerprint.py даёт digest nonignoredworkingtree/gitlinks без содержимого; stage manifest включает его.
SBOM generator create_windows_sbom.py:194actualMSIpayloadfiles,274components,Conangraph+embeddedGo,
license evidence files. build/installer/compliance/hop-windows.cdx.json, license-review.json.
ОфициальнаяCycloneDX1.6 schema validation прошла (.research/sbom-schema-validation.log), jsonschema4.26.0.
SBOM после последнего MSI нужно regenerate. Это не license clearance; libagw1.0.6 archive не содержит
LICENSE/COPYING/SPDX. Никакой код замены ещё НЕ применён. GPL Qt predecessor найден через GitHub:
e4127f47f328f56bbc50f006169fee0565048d89, .research/legacy-gatewayController.{h,cpp} (~725lines),
Go libagw introduced693d00ded5058b7c2d9fcafaf7b817f13ef56409 13окт2026.
Legacy uses amnApp->networkManager (не годитсяQCoreApplication provider-task), broad oldKS allowances
(не переносить!), unboundedreadAll, staticproxyUrl; нужна адаптация Qtworker/лимиты/exactcontrolPlane.
Текущая рабочая libagw реализация не менялась. Source history .research/gateway-history.json.

## Дальше

В первую очередь получить строгий GUI disconnect result и исправить реальную причину, еслиесть.
Затем prepared reboot/BFE, totalprocessresources/performance, license notices/опциональнаязаменаagw,
проверка fullpublicDAT liveUI, transportUDP VLESS, корректная совместимостьIPv6, installerupgrade/rollback.
Собственный online updater/подпись/публичныйrelease требуют отдельной инфраструктуры; не включать
в обязательныйscope молча, пользователь просил auto-update именно подписок, публикацию не разрешал.
Host Amnezia остаётся активной. Agent свежий — ручнойперезапуск просить только приистечении/перезагрузке.

## Пауза по просьбе пользователя
Пользователь попросил продолжить позже при остатке5%. Новые задания не запускать.
Cell405 завершён: MSI CAD33C1B1B64C19D0D2C38E274A8CB85DD7C8959E893CBF74C7EDF5DA6B8A1E8.
Последняя уже запущенная серия InstallerInstall + InstalledGui: exec session41594; результаты .research/strict-gui-install-result.txt и .research/strict-gui-awg-result.txt. Прочитать их при продолжении. Ограниченный VM-тест сам выполняет штатную/аварийную очистку.
PC не выключен; коммиты/публикация/кредит сброса не выполнялись. Агент не продлевался.
