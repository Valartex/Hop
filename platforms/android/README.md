# Hop для Android

Android использует те же C++/Qt/QML модули из `shared/client`, что и Windows.
Здесь находятся приложение Gradle/Kotlin, JNI, VpnService, адаптер маршрутизации
и скрипты Android. Общий AWG-парсер и стек сессии — `shared/native/awgprobe`.
Диагностические ядра запускаются в отдельном процессе, не в процессе интерфейса.

## Сборка на Windows

Проверенный набор в корне репозитория:

- `.tools/build-python`: Python 3.13 с пакетами из `hop/requirements-build.txt`;
- `.tools/go`: Go 1.27.2;
- `.tools/Qt/6.10.1/msvc2022_64`: host Qt с qtremoteobjects, qt5compat, qtshadertools;
- `.tools/Qt/6.10.1/android_arm64_v8a`: Qt той же версии и тех же модулей;
- `.tools/jdk17`: Microsoft OpenJDK 17;
- `.tools/android-sdk`: platform-tools, platforms;android-36, build-tools;36.0.0,
  ndk;27.2.12479018. Для сборки Conan на Windows также требуется Git for Windows/bash.

Скрипт ограничивает переменные SDK/JDK/Go/Conan/Gradle и временные каталоги
процессом сборки. Установка системной службы Windows и запуск VPN не выполняются.

```powershell
# Отладочный APK с отдельным applicationId Hop:
& platforms/android/scripts/build.ps1

# Release AAB для дальнейшей публикации:
& platforms/android/scripts/build.ps1 -Release -Bundle

# Необязательное зеркало Maven Central при ошибках доступа:
& platforms/android/scripts/build.ps1 -Release -Bundle `
    -MavenCentralMirror https://maven-central.storage-download.googleapis.com/maven2
```

Результаты: `build/android/artifacts/`. JSON рядом с пакетом содержит SHA-256,
размер, ABI и результат проверки ELF на страницы памяти 16 КБ.
Исходный APK также проверяется `apksigner verify --print-certs` из Android Build Tools.
Для изменения значков из существующего логотипа есть `scripts/generate-branding.py`
(Pillow требуется только при регенерации ресурсов).

По умолчанию release AAB не подписан. Подпись можно подключить переменными
`QT_ANDROID_KEYSTORE_PATH`, `QT_ANDROID_KEYSTORE_ALIAS`, `QT_ANDROID_KEYSTORE_STORE_PASS`
и, при отдельном пароле ключа, `QT_ANDROID_KEYSTORE_KEY_PASS`. Keystore и пароли
хранятся вне исходников. Публикация в Google Play скриптом не выполняется.
До первой публикации нужно закрепить applicationId (`io.github.valartex.hop`
по умолчанию), ключ подписи и versionCode. Сейчас Android имеет версию 0.3.0.2,
versionCode 3, minSdk 28, targetSdk 36, ABI arm64-v8a.

## Проверка на телефоне

Сборка и автономные тесты не подтверждают реальное VPN-подключение и обход БС.
Порядок приёмки и исследование провайдеров:
`docs/hop/android-client-proposal-2026-10-29.md` в корне репозитория.
Основное устройство: Oppo A6 Pro / Android 16, Билайн и МТС, Брянская область.
Пользователь подключит USB позднее. Подготовленный APK устанавливается через
ADB после подтверждения доверия компьютеру и разрешения VPN самим пользователем.

Режим ограниченной сети использует отмеченные пользователем профили в разрешённом
пуле. Автоматический режим переходит к ним после отказа обычных кандидатов.
Он не диагностирует причину ограничений и не заменяет доступный входной сервер.
Специальные профили обоих провайдеров нужно подготовить через рабочую сеть.

Системные «Постоянная VPN» и «Блокировать соединения без VPN» включаются в Android
по желанию пользователя. При отсутствии VPN-интерфейса блокировка охватывает
также Direct-исключения. Полный IPv6-туннель не входит в текущую версию;
IPv6 блокируется при активном VPN. Поведение при смене сети, уничтожении процесса,
сне и ограничениях батареи проверяется отдельно на смартфоне.
