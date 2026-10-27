# AWG-кандидаты и новый Windows MSI

MSI SHA256: `90eeb42973e23a69c2a16e3eb1a3bec0d20d7cbcc93178d174358a774cbaabad`.
Windows 10 VirtualBox, ограниченный вручную запущенный агент. Хост и Amnezia не менялись.

AWG-проверка запускает отдельный Go-процесс с сетевым стеком в памяти, без системных
адаптеров/маршрутов/служб. Профиль передаётся через stdin. Доступность подтверждается
проверенным DNS-ответом внутри шифрованного AWG; одного handshake недостаточно.
IPv4-mapped endpoint приводится к IPv4. При холодном старте допускаются три DNS-запроса,
общий deadline10с. Размеры мусорных пакетов/CPS ограничены; непроверяемые профили дают
unsupported. Во время рабочей VPN-сессии проверка отложена: повторное использование
ключа может перенести endpoint сервера. Не более одной AWG-проверки и двух проверок всего.
API-конфигурации ради измерений не выпускаются. Задержка не является throughput.

- `8d8af9c7572d4512b79ad9460e833296`: реальный encrypted DNS1021мс; deferred при
  active flag; молчащий DNS — unreachable; адаптеры не изменились, exit0/recovery0.
- `40c2ff4a67f044bd94e631042d8e9f84`: удаление предыдущего714... с guard12→0,
  собственные файлы/SCM удалены, exit0/recovery0. Старый MSI сохранён отдельно.
- `0b64f116328c4b18a69d5576b4e5304e`: установка нового MSI, SCMauto/path/hash, exit0/recovery0.
- `2b00bcb8932f446ebd7bfa42d756d53c`: обычный GUI AWG, candidate reachable1022мс до
  подключения и deferred после; VPN/Direct, live Block edit/reconnect,4МиБ SHA verified,
  отключение3,28с, guard0/UI clear/ACL denied/QML0, exit0/recovery0.
- `c1d1865b71094b6f9a0c053e98e3e7a4`: полный GUI BlancVPN/VLESS, отключение1,49с,
  Block/Direct/reconnect/guard0/UI clear/ACL/QML0, exit0/recovery0.
- `6aac6b21812e48b5a0aa6a61ce49853c`: новый GUI AWG восстановился после реального DOWN
  гостевого uplink на4с и UP того же интерфейса; затем полный GUI-сценарий/guard0, exit0/recovery0.

Все8nativeHopCTest PASS19,05с; Go4 test functions PASS, включая ограничения входа,
отсутствие утечки ключей в ошибках и проверку ID/вопроса/QR/rcode DNS. Первоначальные
отрицательные сетевые пробы сохранены; они не объявлялись успешными.

Новые суммарные секундные resource samples (включая Go/Qt helper, если попал в выборку):
AWG working/private283/325МиБ, CPU15% одного ядра за47,71с; VLESS233/237МиБ,
CPU7,7% за26,74с. Это startup/edit/reconnect/transfer, не длительный idle.
AWG4МиБ за4,885с:6,87Мбит/с на локальном стенде, не предел скорости провайдера.

Пакет установлен в VM. Это проверенный текущий кандидат, не публичный релиз и не
заявление об окончательной приёмке всех Windows-требований. Full IPv6, длинный idle,
upgrade/rollback и лицензионная проверка libagw остаются открытыми. Постоянная защита
до запуска Hop/после reboot исключена пользователем и не возвращается в обязательный объём.
