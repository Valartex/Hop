# Hop

VPN-клиент для Windows на C++ / Qt 6 / QML, основанный на Amnezia VPN.
Текущая проверенная локальная версия: **0.2.0.20**. Проект находится в разработке.

## Возможности

- Подписки и локации VLESS / Reality и AmneziaWG.
- Маршрутизация по доменам, IP, GeoIP и GeoSite.
- Автоматическое переключение локаций с настройкой приоритетов и порога задержки.
- Проверка доступности и недельная история времени ответа.
- Интерфейс на русском, английском и китайском языках.

## Работа с исходниками

```powershell
git clone --recurse-submodules https://github.com/Valartex/Hop.git
cd Hop
```

Сборка Windows: [hop/scripts/build-windows.ps1](hop/scripts/build-windows.ps1).
Скрипт использует MSVC 2022, Qt 6.10.1, Python, CMake, Ninja, Conan, Go и Zig;
пути к локальным инструментам настраиваются через переменные `HOP_*`
в [enter-build-env.ps1](hop/scripts/enter-build-env.ps1).
Ручная сборка CI описана в [.github/workflows/hop-windows.yml](.github/workflows/hop-windows.yml).
Унаследованные workflow публикации не запускаются автоматически при push.

Текущее состояние, проверки и ограничения: [HOP.md](HOP.md).
План работ: [docs/hop/roadmap.md](docs/hop/roadmap.md).

Локальные сборки, инструменты, журналы, данные подписок и результаты VM-проверок
в репозиторий не включаются. Готовые релизы пока не публикуются.

## Основа и лицензия

Upstream: [amnezia-vpn/amnezia-client](https://github.com/amnezia-vpn/amnezia-client).
Исходный README сохранён в [docs/upstream/README.amnezia.md](docs/upstream/README.amnezia.md).
Лицензия проекта — [GPLv3](LICENSE); уведомления о сторонних компонентах —
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
