# Продолжение Windows, 15 октября

Рабочая ветка `codex/hop`, базовый commit `3e8077f2cae9ba7046a99025e303351e0fd31a8e`.
Все изменения оставлены без коммита. Другие платформы отложены.

## Проверенное состояние

Актуальные Hop и тестовые программы собраны, шесть наборов `ctest -R Hop` прошли.
Последние журналы: `.research/final-current-build.log`,
`build/windows/client/tests/Testing/Temporary/LastTest.log`.
Сборка подготовлена в `build/windows-test`, манифест создан 14 октября в 20:02 UTC.
Точные результаты сетевых запусков и их манифесты — в `.research/vm-runs/`.
Основной отчёт: [переключение и защита](windows-selection-validation-2026-10-14.md).

В последней правке добавлен `SecureQSettings::setValueChecked` и публикация снимка
HTTP-подписки после успешного сохранения. Проверены отказ записи и независимое чтение.
После изменения общего заголовка настройки пересобирался клиент; перед следующей
проверкой службы нужно также собрать `Hop-service` из актуального дерева и заново
подготовить пакет. Проверенный ранее бинарник службы уже содержит ограничение и
очистку привилегированного IPC.

## Ограничения среды

Никогда не отключать и не перенастраивать Amnezia, маршруты, DNS или firewall хоста.
Системные тесты — только Windows 10 VirtualBox, UUID
`b79c0594-8e7d-42f6-85b6-3b9784efdb1c`. Тестовый агент запущен пользователем заново
14 октября около 23:09 МСК, срок 4 часа. Проверять свежесть
`.research/vm-exchange/HopJobs/agent-status.json`. Поддерживаемые задания:
`baseline`, `guard-fault`, `stop`; интерфейс и безопасная передача дополнительных
данных описаны скриптами `hop/scripts/send-vm-test-key.ps1` и `vm-test-agent.ps1`.
Не продлевать ограниченный агент самостоятельно. Не сохранять ключи или ссылки в файлы/argv.
Не менять stage во время копирования или выполнения задания; сначала архивировать
результат и соответствующий ему манифест.

Планировщик текущего чата: automation ID `hop`, один запуск 15 октября в 02:15 МСК,
после сброса лимита в 02:11:08. После пробуждения отключить эту автоматизацию.
Не расходовать имеющийся кредит сброса. ПК не выключать: требуется локальное
продолжение. Автоматический сон при питании от сети уже отключён.

## Следующая работа

1. Проверить разделение Go runtime в службе. `dumpbin /dependents Hop-service.exe`
   показывает немедленный импорт `amnezia_xray.dll`, а тот же EXE в режиме
   `tunneldaemon` загружает AWG `tunnel.dll`. Нужно проверить реально загруженные
   модули собственного AWG-процесса и исключить одновременную загрузку двух Go DLL
   (кандидат: MSVC delay-load Xray). После изменения повторить AWG → VLESS в VM.
2. Реальное применение DAT и единая политика Direct/VPN/Block/DNS. Текущий DAT-каталог
   и protobuf-компилятор работают, маршруты не применяются. Не заменять GeoSite
   статическим разрешением доменов в IP: это теряет regex/поддомены и смешивает
   домены на общих адресах CDN. Для AWG требуется проверенный общий механизм
   доменной маршрутизации; штатный kernel AWG сам домены не маршрутизирует.
3. Обновление наборов, инсталлятор, DNS/IPv6, сон/смена сети/BFE/перезагрузка,
   измерение ресурсов. Подробные критерии — в roadmap.

Проверка упаковки начата: установлен .NET SDK 8.0.426; WiX/NSIS пока не обнаружены.
Публичный репозиторий `https://github.com/amnezia-vpn/libagw` доступен, исходный рецепт
указывает tag `v1.0.6` и SHA256 архива
`148f421e20e31b53c7686a29a368e03720c41776facce457c55748d8b2974ddb`.
В готовом Conan-пакете libagw отсутствует каталог лицензий, рецепт не копирует LICENSE.
Перед распространением проверить лицензионные файлы точной версии, происхождение
бинарников и комплект corresponding source. Отсутствие файла в пакете само по себе
не доказывает отсутствие лицензии у проекта.

## Пробуждение 15 октября (живое продолжение)

Automation `hop` успешно переведена в PAUSED после единственного пробуждения.
Лимит сбросился, кредит не расходовали. Старый VM-agent оказался остановлен;
пользователь вручную запустил новый около 02:21 МСК (23:21 UTC), срок 4 часа.
Не продлевать самостоятельно. Последняя проверка ready sequence 5 около 23:57 UTC.

Go runtime исправлен явной QLibrary-загрузкой, AWG → VLESS повторно прошёл.
Подробный новый отчёт — windows-routing-validation-2026-10-15.md.
Новые компилятор TrafficPolicy и ежедневное обновление DAT с protected helper готовы,
нативные тесты прошли. Реальные оба DAT под удержанной защитой также прошли в VM.
Frontend Xray → native AWG с разными VPN/Block доменами на одном IP проверен в VM,
но в основной GUI он ещё не подключён. Основной следующий шаг — составной AWG/frontend
и контроллер пользовательской политики с DAT-категориями.

ВАЖНО: auto-review отклонила широкое dynamic-разрешение TCP/UDP для службы.
Команда не исполнялась. Допустимая узкая альтернатива уже реализована: per-socket
Direct, шесть условий (service AppID, local IF/address, remote IP/port/protocol),
динамическое время жизни сокета и bounded 512. Go wrapper hop/native/xray добавляет
C API hop_xray_setdirectcallbacks, открывает разрешения только для hop-direct,
закрывает при Close/ошибке dial, не даёт UDP расширять endpoint.
Go unit-тесты прошли. build/hop-xray/amnezia_xray.dll собран Zig cc + Go.
Скрипт build-xray-bindings.ps1 воспроизводит сборку; stage -PolicyCore копирует DLL/лицензии.
По умолчанию обычный stage пока перезаписывает её upstream DLL! Всегда -PolicyCore
для новой системной политики и проверяйте манифест. Не обходить rejection широкими правилами.

Текущая новая VM-проба -Policy дополнена domain Direct на собственном TCP loopback-пире
хоста (не TUN/маршруты хоста). Другой процесс должен остаться заблокирован, filter count
1 с шестью точными условиями; после закрытия сокета 0. -Policy -GuardFault должен
проверить исчезновение этих фильтров при аварии главной службы. Эти проверки ещё идут.
Agent recovery уже умеет останавливать/удалять только SCM AWG с точным образом Hop Test.
Обязательно архивировать JSON/логи и манифест перед следующей заменой stage.

## Состояние вечером (около 20:35 МСК)

Пользователь сообщил о пустом VBOXSVR после перезапуска VM; transient shares восстановлены.
Новый агент запущен пользователем около 18:48 МСК (15:48 UTC), срок до 22:48 МСК.
Самостоятельно не продлевать. Автоматизация hop остаётся PAUSED, ПК не выключать.

Общая политика подключена к CoreController/ConnectionController, новому QML экрану
и составному RoutedAwgProtocol. Настройки публикуются только после compile/checked-save;
активные категории защищены от удаления обновлением DAT. Проверены полный AWG TUN
с FakeDNS/GeoSite/Direct/UDP и положительная/отрицательная настоящая DNS health,
а также реальный VLESS с GeoIP/GeoSite. Подробные ID — в routing-validation.
Петля on-link endpoint исправлена; native AWG владеет своим обходом сервера.
stage-windows теперь по умолчанию использует PolicyCore, build-windows строит bindings.

MSI install / ordinary GUI / SCM VLESS / uninstall проверены в VM, исходный MSI SHA
4b4358e57b4afa037b95d800ae9686b68a4f4b0d120e83be9b1fa62049dd430b.
GUI idle: 129/89 МиБ working/private, 0% одного CPU за 3 сек. Снимок в vm-exchange.
После uninstall файлов/SCM нет, удержанные 12 фильтров сняты до recovery. GUI VPN click
с реальным URL ещё не проверен, только обычный GUI и native SCM сетевой тест отдельно.

Сейчас идёт новая сборка `.research/ipc-peer-build.log`: Windows IPC ограничен image path
своего Hop.exe; дополнительная WindowsVpnProbe допустима только в VirtualBox и точном
Program Files/Hop Test. Проверяются control/process/native daemon каналы. Новый
hop_foreign_ipc_probe отвергается; отрицательная VM-проба подготовлена, ещё не запущена.
Служба принудительно убирает параметры файла журнала Xray. Собрать/скопировать новый
helper в VmTests stage, выполнить IpcScope, затем -CrossSelection -RoutedSelection.
После этого MSI нужно пересобрать с новой службой и повторить фазы install/verify/remove.
Внешних публикаций/коммитов нет. Остались полный GUI VPN click, аварийные маршруты,
IPv6/DNS leak capture, сон/смена сети/BFE/reboot и измерения VPN/throughput.

## Продолжение около 21:00 МСК

IPC-hardened build прошёл 8 CTest и отрицательную VM-пробу
`53fc34fcad8e493aa815cee140c267b2` (control/process/native reject чужого EXE).
stage включает foreign helper только для VmTests; MSI исключает его и его hash.

Патч динамического времени жизни туннельных WFP-разрешений сначала отклонён
auto-review, не применён. Пользователь затем явно разрешил его в async-ответе.
Теперь existing own IF permits weight1500 динамические, persistent deny сохранены.
Failure cleanup Xray/AWG применяет guard без туннельного разрешения.
`683cba03bc6b479591c2b6dd06b8123f`: Direct шесть условий, после аварии службы
Direct=0/tunnel permits=0 и физическая блокировка. Recovery0.
`deeed9a5def4472e93c32d2362703500`: составной AWG→VLESS с общей политикой,
13 persistent filters между локациями, Direct HTTP200 на резервной, cleanup0.
`4eaba1054a5046928cd11880580700f6`: настоящий BlancReality с GeoIP/GeoSite,
DNS health и cleanup0. Результаты и точные манифесты в .research/vm-runs.

Новый MSI SHA256 `1356ac4fc84fd95c4a980f2fb4678aa36055a6e2f40be0378f8d3c2739467404`.
Он содержит новую службу и payload manifest без test-exe. Install
`a59fdeca32164a45bcdd5c8f03b6b2ea` прошёл: MSI0, own SCM auto-start/path/hash=true.
Verify `9d15431c41734f02aed2c36237ef542b` получил единичный доменный timeout Qt4;
тот же MSI без правок на повторе `d1e38c87c5ee4ddda6a13e9191958753` прошёл GUI,
IPC, QML и VLESS VPN/Direct/Block/DNS. Отрицательный результат сохранён.
Remove `a8ac3333e0de460faa7ffeb682d22201` прошёл: 12 фильтров до удаления,
MSI0, files/service removed=true, own WFP0 до recovery. Установленной SCM больше нет;
обычные AWG jobs теперь не конфликтуют с Test -exec.
Файлы .research/wfp-msi-*-result.txt, архивы .research/vm-runs.
Агент текущего пользовательского сеанса до ~22:48 МСК, самостоятельно не продлевать.
Остаток лимита 3% в 21:04 МСК, reset credit не использован. Automation PAUSED.
PC не выключать; сеть/Amnezia хоста не трогать. Windows ещё не готов к релизу.

Следующие конкретные работы: настоящий GUI connect/disconnect с подпиской и live
изменением политики; аварийный journal/cleanup физических /32; DNS/IPv6 capture,
сон/смена сети/BFE/reboot в VM; connected ресурсы/throughput; лицензии libagw/SBOM,
обновление/rollback. Pinned mode сейчас не переподключает автоматически ту же локацию
после health failure — нужно исправить и покрыть смысловым тестом.
Не считать одиночный GUI screenshot или отдельную native SCM-пробу полным GUI VPN тестом.
