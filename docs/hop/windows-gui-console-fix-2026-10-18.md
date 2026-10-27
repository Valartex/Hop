# Исправление консольного окна при запуске Hop

Пользовательская проверка 18 октября выявила дефект: обычный запуск Hop.exe
открывал терминальное окно с диагностическим выводом.

## Причина и изменение

В client/CMakeLists.txt флаг /SUBSYSTEM:WINDOWS задавался через общие
CMAKE_EXE_LINKER_FLAGS, но цель приложения не имела свойства WIN32_EXECUTABLE.
CMake добавлял /subsystem:console после общих флагов. В старом Hop.exe
с SHA256 d574da1b15ae9618c3dc1d2042ecafa743fcbf036fbcaf389a99e1c2982cf4e2
поле PE Subsystem действительно равно 3 (Windows CUI).

Общие флаги /SUBSYSTEM и /ENTRY заменены свойством WIN32_EXECUTABLE TRUE
только у цели графического клиента. Qt предоставляет штатную точку входа.
Консольные диагностические инструменты остаются консольными.

Существующий VM-сценарий GUI дополнен полем noConsole, которое проверяет
GetConsoleWindow() внутри самого Hop. Успех сценария теперь требует noConsole=true.
Запуск проверяющим процессом не скрывает консоль специальными флагами создания.

Справка: [CMake WIN32_EXECUTABLE](https://cmake.org/cmake/help/latest/prop_tgt/WIN32_EXECUTABLE.html),
[Qt qt_add_executable](https://doc.qt.io/qt-6/qt-add-executable.html).

## Проверка

Сборка и обновление runtime в VM выполнены без установщика. Пользователь
подтвердил штатное закрытие Hop перед заменой. Сеть и Amnezia основного ПК
не изменялись.

- Новый Hop.exe SHA256: daf28fb6650c1103177c8258def06a9a9aeca05ba4f0f19db25f107caca53713.
- Hop-service.exe не изменён: 8b6e7fa3eae73857bec9de37dd3131f4a0ff5d096e0b8f1792f30dc9ade1f306.
- Новый Hop.exe: PE Subsystem=2 (Windows GUI). Диагностический
  hop_windows_vpn_probe.exe: Subsystem=3 (консоль).
- RuntimeStage 67236d5aa2574f63967e341b1dc72891: exit0/recovery0,
  оба хеша совпали, собственная служба работает, защита очищена.
- HopNativeDashboard и HopXrayLifecycle пересобраны и прошли 2/2 за 10.85 с;
  .research/windows-gui-subsystem-native-tests-2026-10-18.txt.
- Обычный GUI с BlancVPN: 2103a3194ce744f19a7eef333a4e75d6, exit0/recovery0.
  noConsole=true, ordinaryUser=true, импорт/подключение/Direct/live Block/
  переподключение/отключение прошли. Отключение 2659 мс, guardAfterDisconnect=0,
  protectionUiCleared=true, qmlWarnings=0.
- Обычный GUI с AWG 3.1: ae98439f6a8a459888b3572f3b55f70a, exit0/recovery0.
  noConsole=true, ordinaryUser=true, кандидат проверен за 1007 мс;
  подключение/Direct/live Block/переподключение/отключение прошли.
  Отключение 3942 мс, guardAfterDisconnect=0, protectionUiCleared=true,
  qmlWarnings=0; получение 4 МиБ подтверждено SHA256.

Первый AWG-прогон 403c29df1ce24a7ca284aa24a26aa34f с локальным стендом внутри
sandbox завершился отрицательно: кандидат недоступен, стенд получил 0 байт,
noConsole=true, recovery0. Повтор того же сценария со стендом вне sandbox прошёл.
Стек сети хоста не перенастраивался; стенд открывает только пользовательские сокеты.

Текущий путь для пользовательской проверки в VM:
C:\Program Files\Hop VM Install\Hop.exe. Старый MSI в stage не обновлялся.
