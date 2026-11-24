#pragma once
#ifdef Q_OS_WIN
#include <QApplication>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTimer>
#include <QRegularExpression>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <psapi.h>
#include <shobjidl.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#include <functional>
#include <algorithm>
#include "core/controllers/coreController.h"
#include "core/locations/locationCoordinator.h"
#include "core/locations/endpointHealthController.h"
#include "core/subscriptions/httpSubscriptionsController.h"
#include "core/utils/ipcClient.h"
#include "core/utils/networkUtilities.h"
#include "ui/controllers/connectionUiController.h"
#include "hopReadOnlyCatalogTest.h"

namespace hop {
// Включается только main-проверкой VirtualBox. Обычные стенды используют память;
// отдельная API-проверка сохраняет выданный профиль в обычных настройках Hop.
inline int runUiVpnTest(CoreController *core, QQmlApplicationEngine *engine, const QString &pipe, const int &warnings)
{
    QJsonObject result;
    QStringList warningDetails;
    const auto warningConnection = QObject::connect(engine, &QQmlEngine::warnings, core, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) {
            if (warningDetails.size() >= 8) { break; }
            auto description = error.description();
            description.replace(QRegularExpression(QStringLiteral("(?:https?|vless|vpn)://\\S+")), QStringLiteral("[redacted]"));
            warningDetails.append(QStringLiteral("%1:%2 %3").arg(error.url().fileName()).arg(error.line()).arg(description.left(300)));
        }
    });
    const auto disconnectWarnings = qScopeGuard([&]() { QObject::disconnect(warningConnection); });
    QElapsedTimer clock; clock.start();
    int testBudgetMs = 95000;
    auto *catalog = core->findChild<LocationCatalog *>();
    auto *session = core->findChild<LocationCoordinator *>();
    auto *policy = core->findChild<TrafficPolicyController *>();
    auto *connection = core->findChild<ConnectionController *>();
    auto *connectionUi = core->findChild<ConnectionUiController *>();
    auto *subscriptions = core->findChild<HttpSubscriptionsController *>();
    auto *notifications = core->findChild<NotificationHandler *>();
    QJsonArray sessionEvents;
    QJsonArray transportEvents;
    QObject sessionDiagnostics;
    QObject::connect(connection, &ConnectionController::connectionStateChanged, &sessionDiagnostics, [&](Vpn::ConnectionState state) {
        if (transportEvents.size() < 80) { transportEvents.append(QJsonObject{{"ms", clock.elapsed()}, {"state", int(state)},
            {"uiPending", connectionUi->isDisconnecting()}, {"uiCancelling", connectionUi->isCancelling()}}); }
    });
    QObject::connect(session, &SelectionSession::changed, &sessionDiagnostics, [&]() {
        if (sessionEvents.size() < 32) { sessionEvents.append(QJsonObject{{"ms", clock.elapsed()}, {"state", int(session->state())},
            {"status", session->status()}, {"transportConnected", connection->isConnected()}}); }
    });
    QString notificationTitle;
    QString notificationMessage;
    const auto notificationConnection = QObject::connect(notifications, &NotificationHandler::notificationShown, core,
        [&](const QString &title, const QString &message) { notificationTitle = title; notificationMessage = message; });
    const auto disconnectNotifications = qScopeGuard([&]() { QObject::disconnect(notificationConnection); });
    auto *window = engine->rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine->rootObjects().first());
    const auto wait = [&](const std::function<bool()> &ready, int maximum) {
        if (ready()) { return true; }
        QEventLoop loop; QTimer poll, deadline;
        poll.setInterval(20); deadline.setSingleShot(true);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (ready()) { loop.quit(); } });
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        deadline.start(std::max(1, std::min(maximum, int(testBudgetMs - clock.elapsed())))); poll.start(); loop.exec();
        return ready();
    };
    const auto item = [&](const QString &name) {
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *parent) -> QQuickItem * {
            if (parent->objectName() == name) { return parent; }
            for (auto *child : parent->childItems()) { if (auto *found = find(child)) { return found; } }
            return nullptr;
        };
        return window ? find(window->contentItem()) : nullptr;
    };
    const auto click = [&](const QString &name) {
        result.insert("stage", name);
        QElapsedTimer settled; settled.start();
        wait([&]() { return settled.elapsed() >= 400; }, 500);
        auto *control = item(name);
        if (!control || !control->isVisible() || !control->isEnabled() || control->width() <= 0 || control->height() <= 0) { return false; }
        // Клавиатурная активация проходит через настоящий Qt Quick Button и его обработчики.
        control->forceActiveFocus(Qt::TabFocusReason);
        if (!control->hasActiveFocus()) { return false; }
        bool observed = false;
        const auto activation = QObject::connect(connectionUi,&ConnectionUiController::connectButtonClicked,connectionUi,[&]() { observed = true; });
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(window, &press); QApplication::sendEvent(window, &release);
        QObject::disconnect(activation);
        if (name == QStringLiteral("hopConnectButton")) { result.insert("connectSignalObserved",observed); return observed; }
        return true;
    };
    HANDLE token = nullptr; TOKEN_ELEVATION elevation {}; DWORD size = 0;
    const bool ordinary = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)
        && GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && !elevation.TokenIsElevated;
    if (token) { CloseHandle(token); }
    result.insert("ordinaryUser", ordinary);
    const bool noConsole = GetConsoleWindow() == nullptr;
    result.insert("noConsole", noConsole);
    const auto guardCount = []() { return IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->trafficGuardFilterCount(); return reply.waitForFinished(3000) ? reply.returnValue() : -1;
    }, []() { return -1; }); };
    const auto cancelAttempt = [&](int delay) {
        if (!wait([&]() { return item("hopConnectButton") && item("hopConnectButton")->isEnabled(); }, 3000)) { return false; }
        QTimer cancellation; cancellation.setSingleShot(true);
        bool requested = false, feedback = false;
        const auto feedbackObserver = QObject::connect(connectionUi, &ConnectionUiController::connectionStateChanged, &cancellation, [&]() {
            if (!requested || !connectionUi->isDisconnecting()) { return; }
            auto *button = item("hopConnectButton"); auto *status = item("hopConnectionStatus");
            const auto buttonText = QCoreApplication::translate("HopDashboard", connectionUi->isCancelling() ? "Отменяется…" : "Отключается…");
            const auto statusText = QCoreApplication::translate("HopDashboard", connectionUi->isCancelling() ? "Отмена подключения…" : "Отключение…");
            feedback |= button && status && !button->isEnabled() && button->property("text").toString() == buttonText
                && status->property("text").toString() == statusText;
        });
        const auto disconnectFeedback = qScopeGuard([&]() { QObject::disconnect(feedbackObserver); });
        QObject::connect(&cancellation, &QTimer::timeout, core, [&]() { requested = true; connectionUi->closeConnection(); });
        QElapsedTimer elapsed; elapsed.start();
        if (!click("hopConnectButton")) { return false; }
        cancellation.start(delay);
        const auto clean = [&]() { return requested && !session->running() && !connection->isConnected()
            && !connectionUi->isConnectionInProgress() && guardCount() == 0; };
        bool stopped = wait(clean, 15000);
        if (stopped) {
            // После возврата незавершённого start соединение не должно ожить снова.
            QEventLoop settle; QTimer::singleShot(750, &settle, &QEventLoop::quit); settle.exec();
            stopped = clean();
        }
        auto rounds = result.value("cancellationRounds").toArray();
        rounds.append(QJsonObject{{"delayMs", delay}, {"elapsedMs", elapsed.elapsed()}, {"clean", stopped}, {"stoppingFeedback", feedback},
            {"sessionRunning", session->running()}, {"transportConnected", connection->isConnected()},
            {"uiInProgress", connectionUi->isConnectionInProgress()}, {"guardFilters", guardCount()}});
        result.insert("cancellationRounds", rounds);
        return stopped && feedback;
    };
    bool passed = false;
    const auto finish = qScopeGuard([&]() {
        result.insert("sessionStateBeforeCleanup", session ? int(session->state()) : -1);
        result.insert("sessionEvents", sessionEvents);
        result.insert("sessionStatusBeforeCleanup", session ? session->status() : QString());
        result.insert("connectionErrorBeforeCleanup", connectionUi ? int(connectionUi->getLastConnectionError()) : -1);
        result.insert("transportEvents", transportEvents);
        // При любом отрицательном результате просим штатное отключение до выхода GUI.
        if (!passed && session) { session->disconnectManually(); }
        const bool disconnected = passed || (connection && connectionUi && wait([&]() {
            return !connection->isConnected() && !connectionUi->isConnectionInProgress() && guardCount() == 0;
        }, 10000));
        result.insert("disconnected", disconnected); result.insert("qmlWarnings", warnings);
        result.insert("qmlWarningDetails", QJsonArray::fromStringList(warningDetails));
        result.insert("elapsedMs", clock.elapsed());
        const auto path = QDir::tempPath() + QStringLiteral("/hop-ui-vpn-%1").arg(QCoreApplication::applicationPid());
        result.insert("imageSaved", window && window->grabWindow().save(path + ".png"));
        QSaveFile file(path + ".json"); const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
        if (file.open(QIODevice::WriteOnly)) { file.write(bytes); file.commit(); }
    });
    if (!ordinary || !catalog || !session || !policy || !connection || !connectionUi || !subscriptions || !window) { return 3; }
    PWSTR appId = nullptr;
    const bool identity = SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&appId)) && appId && QString::fromWCharArray(appId) == QStringLiteral("Hop.Desktop");
    if (appId) { CoTaskMemFree(appId); }
    result.insert("taskbarIdentityHop", identity);
    const auto nativeIcon = reinterpret_cast<HICON>(SendMessageW(reinterpret_cast<HWND>(window->winId()), WM_GETICON, ICON_BIG, 0));
    const auto actualIcon = nativeIcon ? QImage::fromHICON(nativeIcon).convertToFormat(QImage::Format_ARGB32) : QImage();
    const auto expectedIcon = QIcon(QStringLiteral(":/images/hop/hop.ico")).pixmap(actualIcon.size()).toImage().convertToFormat(QImage::Format_ARGB32);
    int comparable = 0, matching = 0;
    if (!actualIcon.isNull() && actualIcon.size() == expectedIcon.size()) {
        for (int y = 0; y < actualIcon.height(); ++y) {
            for (int x = 0; x < actualIcon.width(); ++x) {
                const auto expected = expectedIcon.pixelColor(x, y), actual = actualIcon.pixelColor(x, y);
                if (expected.alpha() < 240) { continue; }
                ++comparable;
                if (qAbs(expected.red()-actual.red()) < 16 && qAbs(expected.green()-actual.green()) < 16 && qAbs(expected.blue()-actual.blue()) < 16) { ++matching; }
            }
        }
        actualIcon.save(QStringLiteral("//VBOXSVR/HopTestResults/hop-window-large-icon-%1.png").arg(QCoreApplication::applicationPid()));
    }
    const bool iconMatches = comparable > 0 && matching >= comparable * 0.98;
    result.insert("nativeLargeIconHop", iconMatches);
    if (!identity || !iconMatches) { return 3; }
    if (!wait([&]() { return connection->isServiceReady() && policy->ready(); }, 8000)) { return 3; }
    result.insert("serviceReady", true);
    if (QCoreApplication::arguments().at(1) == QStringLiteral("--hop-ui-api-vpn-test")) {
        result.insert("persistentApiTest", true);
        QLocalSocket authorization; authorization.connectToServer(pipe);
        if (!authorization.waitForConnected(3000) || !authorization.waitForReadyRead(3000)) { return 3; }
        const auto packet = authorization.readLine(1024);
        const auto authorizationKind = QJsonDocument::fromJson(packet).object().value("importData").toString();
        const bool fresh = authorizationKind == QStringLiteral("HopPersistentApiFreshVpnTest");
        if (!packet.endsWith('\n') || (!fresh && authorizationKind != QStringLiteral("HopPersistentApiVpnTest"))) { return 3; }
        authorization.disconnectFromServer();
        auto *servers = core->findChild<SecureServersRepository *>();
        if (!servers || connection->isConnected() || session->running()) { return 3; }
        QString serverId;
        for (const auto &id : servers->orderedServerIds()) {
            if (servers->apiV2Config(id)) { serverId = id; break; }
        }
        if (serverId.isEmpty()) { return 3; }
        result.insert("stage", "api-cached-connection");
        const auto originalId = catalog->selectedId(); const int originalMode = catalog->mode();
        const auto restoreSelection = qScopeGuard([&]() {
            session->disconnectManually();
            if (wait([&]() { return !connection->isConnected() && !connectionUi->isConnectionInProgress() && guardCount() == 0; }, 10000)) {
                catalog->setMode(LocationCatalog::Manual); catalog->select(originalId); catalog->setMode(originalMode);
            }
        });
        const auto api = servers->apiV2Config(serverId);
        result.insert("apiHadCachedConfiguration", api && api->hasContainers());
        // Каталог проверен отдельной read-only пробой; здесь используем существующий профиль.
        if (!api || !api->hasContainers()) { return 3; }
        QString selected;
        for (const auto &entry : catalog->entries()) {
            if (entry.serverId != serverId || !entry.enabled || entry.apiCountry.isEmpty()) { continue; }
            if (fresh) {
                if (entry.apiCountry != api->apiConfig.serverCountryCode && (entry.country == "NL" || entry.country == "DE")) {
                    selected = entry.id;
                    if (entry.country == "NL") { break; }
                }
            } else if (entry.apiCountry == api->apiConfig.serverCountryCode) { selected = entry.id; break; }
        }
        result.insert("apiFreshLocationRequested", fresh);
        catalog->setMode(LocationCatalog::Manual);
        if (selected.isEmpty() || !catalog->select(selected)) { return 3; }
        core->pageController()->goToPageHome();
        if (!fresh && !cancelAttempt(500)) { return 3; }
        const auto beforeConnect = session->token();
        if (!wait([&]() { return item("hopConnectButton") && item("hopConnectButton")->isEnabled(); }, 3000) || !click("hopConnectButton")) { return 3; }
        // Обработчик кнопки поставлен в очередь; исходный Stopped ещё не результат попытки.
        if (!wait([&]() { return session->token() != beforeConnect || session->running(); }, 3000)) { return 3; }
        const bool connected = wait([&]() { return (session->state() == SelectionSession::Healthy && connection->isConnected())
            || session->state() == SelectionSession::Backoff || !session->running(); }, 85000)
            && session->state() == SelectionSession::Healthy && connection->isConnected();
        result.insert("connectButton", connected); result.insert("apiSessionState", int(session->state()));
        result.insert("apiFailureStatus", session->status());
        result.insert("apiLocationStatus", QJsonObject::fromVariantMap(session->statuses().value(selected).toMap()));
        result.insert("apiConnectionError", int(connection->lastConnectionError()));
        const auto prepared = servers->apiV2Config(serverId);
        bool saved = false;
        if (prepared && prepared->hasContainers()) {
            for (const auto &stored : readExistingApiProfiles()) {
                const auto actual = ApiV2ServerConfig::fromJson(stored.toObject());
                if (actual.hasContainers() && actual.authData.toJson() == prepared->authData.toJson()
                    && actual.apiConfig.serverCountryCode == prepared->apiConfig.serverCountryCode) { saved = true; }
            }
        }
        result.insert("apiConfigurationSaved", saved);
        result.insert("notificationHasLocation", notificationMessage.contains(catalog->entry(selected)->name));
        if (!connected || !saved || guardCount() <= 0) { return 3; }
        QJsonArray connectionEvents;
        QObject diagnosticEvents;
        QObject::connect(connection, &ConnectionController::connectionStateChanged, &diagnosticEvents, [&](Vpn::ConnectionState state) {
            if (connectionEvents.size() < 32) { connectionEvents.append(QJsonObject{{"ms", clock.elapsed()}, {"connectionState", int(state)},
                {"sessionState", int(session->state())}, {"error", int(connection->lastConnectionError())}}); }
        });
        double peakVpnRate = -1;
        QObject::connect(session, &LocationCoordinator::trafficRatesChanged, &diagnosticEvents, [&]() {
            peakVpnRate = qMax(peakVpnRate, session->trafficRates().value("vpnDown", -1).toDouble());
        });
        QNetworkAccessManager network; network.setProxy(QNetworkProxy::NoProxy);
        const auto probe = [&](const QString &key, const QUrl &url, int expected, int timeout) {
            QNetworkRequest request(url); request.setTransferTimeout(timeout); request.setRawHeader("Connection", "close");
            auto *reply = network.get(request);
            const bool complete = wait([&]() { return reply->isFinished(); }, timeout + 1000);
            if (!complete) { reply->abort(); }
            const bool successful = complete && reply->error() == QNetworkReply::NoError
                && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == expected;
            result.insert(key, successful); result.insert(key + "ReplyError", int(reply->error()));
            result.insert(key + "Status", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
            reply->deleteLater(); return successful;
        };
        const bool https = probe("apiHttps", QUrl("https://connectivitycheck.gstatic.com/generate_204"), 204, 10000);
        const bool domain = probe("apiDomainHttps", QUrl("https://one.one.one.one/cdn-cgi/trace"), 200, 7000);
        // Это диагностика: сохранённая пользовательская политика может направлять IP напрямую.
        probe("apiLiteralHttps", QUrl("https://1.0.0.1/cdn-cgi/trace"), 200, 7000);
        QObject measurement;
        bool measured = false;
        QObject::connect(session, &LocationCoordinator::locationSampled, &measurement,
            [&](const QString &id, int) { if (id == selected) { measured = true; } });
        const bool activeCheck = session->checkCurrentLocation(selected) && wait([&]() { return measured; }, 12000);
        result.insert("apiActiveProbeCompleted", activeCheck);
        result.insert("apiMeasuredHealth", QJsonObject::fromVariantMap(session->statuses().value(selected).toMap()));
        result.insert("trafficRatesAvailable", session->trafficRates().contains("vpnDown"));
        result.insert("vpnDownloadObserved", peakVpnRate > 0);
        if (activeCheck && session->statuses().value(selected).toMap().value("state") != "reachable") {
            QProcess diagnostic;
            diagnostic.start(QCoreApplication::applicationDirPath() + QStringLiteral("/hop_tunnel_probe.exe"));
            if (diagnostic.waitForStarted(3000)) {
                const auto dns = prepared->getDnsPair(QStringLiteral("1.1.1.1"), QStringLiteral("8.8.8.8"));
                const auto request = QJsonObject{{"interface", connection->usesRoutingFrontend() ? "Hop-TUN" : "Hop"},
                    {"resolver", connection->usesRoutingFrontend() ? QStringLiteral("198.18.0.1") : dns.first}};
                diagnostic.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n'); diagnostic.closeWriteChannel();
                if (diagnostic.waitForFinished(10000)) {
                    result.insert("apiProbeDetails", QJsonDocument::fromJson(diagnostic.readAllStandardOutput()).object());
                } else { diagnostic.kill(); diagnostic.waitForFinished(1000); }
            }
        }
        window->grabWindow().save(QStringLiteral("//VBOXSVR/HopTestResults/hop-desktop-api-connected-%1.png").arg(QCoreApplication::applicationPid()));
        QElapsedTimer disconnectTime; disconnectTime.start();
        if (!click("hopConnectButton")) { return 3; }
        const bool stopped = wait([&]() { return !session->running() && !connection->isConnected()
            && !connectionUi->isConnectionInProgress(); }, 15000)
            && wait([&]() { return guardCount() == 0; }, 4000);
        result.insert("disconnectButton", stopped); result.insert("manualDisconnectMs", disconnectTime.elapsed());
        result.insert("guardAfterDisconnect", guardCount());
        const bool clear = stopped && wait([&]() { return !session->running() && !connection->isConnected()
            && !connectionUi->isConnectionInProgress() && connectionUi->protectionServiceAvailable()
            && !connectionUi->trafficProtectionActive(); }, 5000);
        result.insert("sessionRunningAfterDisconnect", session->running());
        result.insert("connectionAfterDisconnect", connection->isConnected());
        result.insert("uiProgressAfterDisconnect", connectionUi->isConnectionInProgress());
        result.insert("protectionUiCleared", clear);
        result.insert("connectionEvents", connectionEvents);
        passed = connected && saved && (https || domain) && stopped && clear && noConsole && warnings == 0;
        return passed ? 0 : 3;
    }
    const bool directStartup = catalog->entries().isEmpty() && item("hopAddButton") && item("hopSettingsTab");
    result.insert("directDashboardStartup", directStartup);
    if (!directStartup) { return 3; }
    const auto desktopCapture = [&](const QString &name) {
        QElapsedTimer repaint; repaint.start();
        wait([&]() { return repaint.elapsed() >= 250; }, 350);
        return window->grabWindow().save(QStringLiteral("//VBOXSVR/HopTestResults/hop-desktop-%1-%2.png")
            .arg(name).arg(QCoreApplication::applicationPid()));
    };
    if (!desktopCapture(QStringLiteral("startup")) || !click("hopSettingsTab")
        || !wait([&]() { return item("hopSubscriptionInterval") && item("hopHealthInterval")
            && item("hopSubscriptionInterval")->property("currentValue").toInt() == 360
            && item("hopHealthInterval")->property("currentValue").isValid()
            && item("hopHealthInterval")->property("currentValue").toInt() == 0; }, 3000)
        || !desktopCapture(QStringLiteral("settings")) || !click("hopAboutButton")
        || !wait([&]() { return item("hopAboutPage"); }, 3000)
        || !desktopCapture(QStringLiteral("about")) || !click("hopHomeTab")
        || !wait([&]() { return item("hopAddButton"); }, 3000)) { return 3; }
    result.insert("desktopNavigation", true);
    result.insert("refreshSettingsVisible", true);
    QMenu *tray = nullptr;
    for (auto *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("hopTrayMenu")) { tray = qobject_cast<QMenu *>(widget); break; }
    }
    if (!tray || tray->actions().isEmpty()) { return 3; }
    tray->popup(window->position() + QPoint(200, 200));
    if (!wait([&]() { return tray->isVisible(); }, 1000)) { return 3; }
    tray->grab().save(QStringLiteral("//VBOXSVR/HopTestResults/hop-tray-menu-%1.png").arg(QCoreApplication::applicationPid()));
    bool trayTriggered = false;
    const auto trayClick = QObject::connect(tray->actions().first(), &QAction::triggered, tray, [&]() { trayTriggered = true; });
    const QPointF position(tray->actionGeometry(tray->actions().first()).center());
    const QPointF global(tray->mapToGlobal(position.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(tray, &press); QApplication::sendEvent(tray, &release); QObject::disconnect(trayClick); tray->hide();
    result.insert("trayMenuAction", trayTriggered);
    if (!trayTriggered) { return 3; }
    const auto journalPath = QDir::toNativeSeparators(qEnvironmentVariable("ProgramData") + QStringLiteral("/Hop/routes-v1.json"));
    const auto journal = CreateFileW(reinterpret_cast<LPCWSTR>(journalPath.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool journalReadDenied = journal == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED;
    if (journal != INVALID_HANDLE_VALUE) { CloseHandle(journal); }
    result.insert("protectedJournalReadDenied", journalReadDenied);
    if (!journalReadDenied) { return 3; }
    QLocalSocket source; source.connectToServer(pipe);
    if (!source.waitForConnected(3000) || !source.waitForReadyRead(3000)) { return 3; }
    auto bytes = source.readLine(65537);
    if (bytes.size() > 65536 || !bytes.endsWith('\n')) { return 3; }
    const auto inputPacket = QJsonDocument::fromJson(bytes).object();
    const int lanPort = inputPacket.value(QStringLiteral("lanPort")).toInt();
    if (lanPort && (lanPort < 1024 || lanPort > 65535)) { return 3; }
    const auto input = inputPacket.value(QStringLiteral("importData")).toString();
    if (input == QStringLiteral("HopReadOnlyCatalogTest") || input == QStringLiteral("HopReadOnlyApiHealthTest")) {
        auto *servers = core->findChild<SecureServersRepository *>();
        const auto profiles = readExistingApiProfiles();
        result.insert("catalogReadOnly", true);
        result.insert("apiCatalogSources", profiles.size());
        if (!servers || profiles.isEmpty()) { return 3; }
        QStringList ids;
        for (int row = 0; row < profiles.size(); ++row) {
            const auto profile = profiles.at(row).toObject();
            const auto id = QStringLiteral("api-catalog-%1").arg(row);
            ids.append(id); servers->addServer(id, profile, serverConfigUtils::configTypeFromJson(profile));
        }
        if (input == QStringLiteral("HopReadOnlyApiHealthTest")) {
            auto *health = core->findChild<EndpointHealthController *>();
            if (!health || connection->isConnected() || session->running() || guardCount() != 0) { return 3; }
            QString selected;
            for (const auto &entry : catalog->entries()) {
                if (entry.api && health->canCheckLocation(entry.id)) { selected = entry.id; break; }
            }
            result.insert("apiCachedProbeAvailable", !selected.isEmpty());
            if (selected.isEmpty()) { return 3; }
            const auto serverId = catalog->entry(selected)->serverId;
            const auto before = servers->apiV2Config(serverId);
            const bool accepted = health->checkLocation(selected);
            const bool completed = accepted && wait([&]() { return !health->isBusy(); }, 17000);
            const auto status = health->statuses().value(selected).toMap();
            const auto after = servers->apiV2Config(serverId);
            const bool unchanged = before && after && before->toJson().value(configKey::containers) == after->toJson().value(configKey::containers)
                && before->apiConfig.serverCountryCode == after->apiConfig.serverCountryCode;
            result.insert("apiStandaloneHealth", QJsonObject::fromVariantMap(status));
            result.insert("apiConfigurationUnchanged", unchanged);
            result.insert("guardAfterProbe", guardCount());
            passed = completed && status.value("state") == "reachable" && unchanged && guardCount() == 0;
            return passed ? 0 : 3;
        }
        const bool completed = wait([&]() {
            for (const auto &id : ids) {
                const auto state = session->catalogStatuses().value(id).toMap().value("state").toString();
                if (state != "ready" && state != "error") { return false; }
            }
            return true;
        }, 75000);
        QJsonArray states;
        bool ready = completed;
        for (const auto &id : ids) {
            const auto status = session->catalogStatuses().value(id).toMap();
            const auto api = servers->apiV2Config(id);
            QStringList countryFields;
            if (api) {
                for (const auto &country : api->apiConfig.availableCountries) {
                    for (const auto &key : country.toObject().keys()) {
                        if (!countryFields.contains(key)) { countryFields.append(key); }
                    }
                }
            }
            countryFields.sort();
            states.append(QJsonObject { { "state", status.value("state").toString() }, { "code", status.value("code").toInt() },
                { "activeConfigurations", api ? api->apiConfig.activeDeviceCount : -1 },
                { "maxConfigurations", api ? api->apiConfig.maxDeviceCount : -1 },
                { "countryFieldNames", QJsonArray::fromStringList(countryFields) } });
            ready = ready && status.value("state") == "ready";
        }
        result.insert("apiCatalogStates", states);
        result.insert("apiCatalogLocations", catalog->rowCount());
        result.insert("apiCatalogReady", ready);
        desktopCapture(QStringLiteral("api-catalog"));
        passed = ready;
        return ready ? 0 : 3;
    }
    const auto idleMode = inputPacket.value(QStringLiteral("idleMode")).toString();
    if (!idleMode.isEmpty() && idleMode != "connected" && idleMode != "disconnected" && idleMode != "monitoring") { return 3; }
    bytes.fill(0); bytes.clear();
    const bool awg = input.startsWith(QStringLiteral("[Interface]")) && input.contains(QStringLiteral("# HopInstalledAwgGuiTest"));
    const bool networkTest = awg && input.contains(QStringLiteral("# HopInstalledNetworkTest"));
    if (!networkTest && idleMode.isEmpty()) { source.disconnectFromServer(); }
    const auto measureIdle = [&](const QString &mode) {
        if (idleMode != mode) { return true; }
        source.write("{\"phase\":\"idle-begin\"}\n"); source.flush();
        if (!wait([&]() { return source.canReadLine(); }, 3000)
            || QJsonDocument::fromJson(source.readLine(1024)).object().value("phase") != "idle-ready") { return false; }
        // Во время измерения нет тестового polling: обычный event loop клиента.
        QEventLoop idle; QElapsedTimer elapsed; elapsed.start();
        QTimer::singleShot(30000, Qt::PreciseTimer, &idle, &QEventLoop::quit); idle.exec();
        result.insert("measuredIdleMode", mode); result.insert("measuredIdleMs", elapsed.elapsed());
        source.write("{\"phase\":\"idle-end\"}\n"); source.flush();
        if (!wait([&]() { return source.canReadLine(); }, 3000)) { return false; }
        const auto response = QJsonDocument::fromJson(source.readLine(1024)).object();
        if (!networkTest) { source.disconnectFromServer(); }
        return elapsed.elapsed() >= 29000 && response.value("phase") == "idle-finished" && response.value("accepted").toBool();
    };
    const auto lanAddress = QHostAddress(QStringLiteral("10.0.2.2"));
    const auto lanUrl = QUrl(QStringLiteral("http://10.0.2.2:%1/hop-lan-ok").arg(lanPort));
    const int originalLanInterface = lanPort ? NetworkUtilities::AdapterIndexTo(lanAddress) : 0;
    QHostAddress originalSource;
    for (const auto &address : NetworkUtilities::getGatewayAndIface().second.addressEntries()) {
        if (address.ip().protocol() == QAbstractSocket::IPv4Protocol) { originalSource = address.ip(); break; }
    }
    QNetworkAccessManager network; network.setProxy(QNetworkProxy::NoProxy);
    const auto request = [&](const QUrl &address, int expected) {
        QNetworkRequest request(address); request.setTransferTimeout(10000); request.setRawHeader("Connection", "close");
        auto *reply = network.get(request);
        const bool done = wait([&]() { return reply->isFinished(); }, 11000);
        if (!done) { reply->abort(); }
        const bool fixture = address != lanUrl || !expected || reply->rawHeader("X-Hop-Lan-Test") == "yes";
        const bool ok = done && fixture && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == expected
            && (expected ? reply->error() == QNetworkReply::NoError : reply->error() == QNetworkReply::RemoteHostClosedError);
        reply->deleteLater(); return ok;
    };
    if (lanPort) {
        const bool reachable = originalLanInterface > 0 && request(lanUrl, 204);
        result.insert("lanBeforeConnect", reachable); if (!reachable) { return 3; }
        auto *datasets = core->findChild<RouteDatasetsController *>(); if (!datasets) { return 3; }
        // Полная PRIVATE-категория поставляемого DAT; кэш VM-стенда изолирован от пользовательского.
        const auto dat = QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + QStringLiteral("/geoip.dat"));
        result.insert("stage", "lan-dataset-import");
        if (!datasets->importFile(dat, QStringLiteral("geoip"))
            || !wait([&]() { return !datasets->isBusy() && !policy->busy(); }, 5000) || !datasets->lastError().isEmpty()) {
            result.insert("lanDatasetError", datasets->lastError()); return 3;
        }
        result.insert("stage", "lan-private-rule");
        if (!policy->addRule(QStringLiteral("geoip"), QStringLiteral("PRIVATE"), QStringLiteral("direct"))
            || !wait([&]() { return !policy->busy() && policy->enabled() && policy->rules().size() == 1; }, 5000)) {
            result.insert("lanPolicyError", policy->lastError()); return 3;
        }
    }
    const auto url = QUrl(input);
    if (!awg && (url.scheme() != QStringLiteral("https") || url.host().isEmpty())) { return 3; }
    result.insert("inputKind", awg ? "awg-fixture" : "https-subscription");
    core->importConfigFromData(input);
    if (!wait([&]() { return !subscriptions->isBusy() && catalog->rowCount() > 0; }, 20000)) { return 3; }
    result.insert("subscriptionImported", true);
    if (!awg) {
        const auto servers = core->findChild<SecureServersRepository *>();
        if (!servers) { return 3; }
        const auto native = servers->nativeConfig(catalog->entries().first().serverId);
        if (native) {
            result.insert("subscriptionDisplayName", catalog->serviceInfo(catalog->entries().first().serviceId).value(QStringLiteral("name")).toString());
            result.insert("subscriptionAutomaticName", native->subscription.value(QStringLiteral("sourceNameMode")) == QStringLiteral("automatic"));
        }
    }
    if (!wait([&]() { return item("hopDetailsButton_0"); }, 3000) || !click("hopDetailsButton_0")) { return 3; }
    if (!wait([&]() { return item("hopLocationStats_0") && item("hopLocationStats_0")->isVisible(); }, 2000)) { return 3; }
    desktopCapture(QStringLiteral("location-menu"));
    if (!click("hopLocationStats_0")) { return 3; }
    auto *details = window->findChild<QObject *>(QStringLiteral("hopLocationStatsDialog"));
    const bool detailsOpened = details && wait([&]() { return details->property("visible").toBool(); }, 2000);
    result.insert("locationMenuAction", detailsOpened);
    if (!detailsOpened || !QMetaObject::invokeMethod(details, "close")) { return 3; }
    auto *endpointHealth = core->findChild<EndpointHealthController *>();
    if (awg) {
        if (!endpointHealth || !endpointHealth->available()) { return 3; }
        endpointHealth->checkAll();
        if (!wait([&]() { return !endpointHealth->isBusy(); }, 16000)) { return 3; }
        const auto checked = endpointHealth->statuses().value(catalog->entries().first().serverId).toMap();
        const bool reachable = checked.value("state") == "reachable" && checked.value("latencyMs", -1).toInt() >= 0;
        result.insert("awgCandidateProbe", reachable); result.insert("awgCandidateLatencyMs", checked.value("latencyMs", -1).toInt());
        if (!reachable) { return 3; }
        const bool measured = endpointHealth->history(catalog->entries().first().id).value("count").toInt() > 0;
        result.insert("locationHistoryMeasured", measured); if (!measured) { return 3; }
        if (!networkTest && idleMode.isEmpty()) {
            if (!click("hopDetailsButton_0") || !click("hopLocationStats_0")
                || !wait([&]() { return details->property("visible").toBool() && details->property("statistics").toMap().value("count").toInt() > 0; }, 2000)) { return 3; }
            desktopCapture(QStringLiteral("location-statistics"));
            result.insert("locationStatisticsOpened", true);
            if (!QMetaObject::invokeMethod(details, "close")) { return 3; }
        }
    }
    catalog->select(catalog->entries().first().id);
    // LAN-сценарий проверяет передачу HTTP сам, независимо от DNS-пробы провайдера.
    const int testSelectionMode = inputPacket.value(QStringLiteral("selectionMode")).toInt(-1);
    if (testSelectionMode == LocationCatalog::AllFastest) { testBudgetMs = 105000; }
    catalog->setMode(testSelectionMode == LocationCatalog::AllFastest ? LocationCatalog::AllFastest
        : lanPort ? LocationCatalog::Manual : LocationCatalog::Manual);
    core->pageController()->goToPageHome();
    if (!measureIdle(QStringLiteral("disconnected"))) { return 3; }
    if (!wait([&]() { return item("hopRoutesButton"); }, 3000) || !click("hopRoutesButton")) { return 3; }
    const auto addRule = [&](int kind, int target, const QString &value) {
        result.insert("stage", "route-editor");
        if (!wait([&]() { return item("hopRoutesValue") && item("hopRoutesKind") && item("hopRoutesTarget") && !policy->busy(); }, 3000)) { return false; }
        item("hopRoutesKind")->setProperty("currentIndex", kind); item("hopRoutesTarget")->setProperty("currentIndex", target);
        item("hopRoutesValue")->setProperty("text", value);
        QCoreApplication::processEvents(); const int count = policy->rules().size();
        return click("hopRoutesAdd") && wait([&]() { return !policy->busy() && policy->enabled() && policy->rules().size() == count + 1; }, 4000);
    };
    if (!addRule(1, 1, QStringLiteral("1.0.0.1/32"))) { return 3; }
    const bool orderTest = awg && !networkTest && idleMode.isEmpty();
    if (orderTest && !addRule(1, 2, QStringLiteral("192.0.2.0/24"))) { return 3; }
    core->pageController()->goToPageHome();
    if (!networkTest && idleMode.isEmpty() && testSelectionMode != LocationCatalog::AllFastest) {
        for (int delay : {0, 250, 1500}) { if (!cancelAttempt(delay)) { return 3; } }
    }
    if (!wait([&]() { return item("hopConnectButton") && item("hopConnectButton")->isEnabled(); }, 3000) || !click("hopConnectButton")) { return 3; }
    if (!awg && testSelectionMode != LocationCatalog::AllFastest && wait([&]() { return connection->isConnected(); }, 10000)) {
        QProcess diagnostic;
        diagnostic.start(QCoreApplication::applicationDirPath() + QStringLiteral("/hop_tunnel_probe.exe"));
        if (diagnostic.waitForStarted(2000)) {
            diagnostic.write("{\"interface\":\"Hop-TUN\",\"resolver\":\"198.18.0.1\"}\n"); diagnostic.closeWriteChannel();
        }
        result.insert("initialHttpsDuringVerification", request(QUrl(QStringLiteral("https://www.gstatic.com/generate_204")), 204));
        result.insert("initialLiteralHttpsDuringVerification", request(QUrl(QStringLiteral("https://1.1.1.1/cdn-cgi/trace")), 200));
        if (diagnostic.state() != QProcess::NotRunning) { diagnostic.kill(); diagnostic.waitForFinished(1000); }
        result.insert("initialDnsProbe", QJsonDocument::fromJson(diagnostic.readAllStandardOutput()).object());
    }
    if (!wait([&]() { return session->state() == SelectionSession::Healthy && connection->isConnected(); }, testSelectionMode == LocationCatalog::AllFastest ? 80000 : 35000)) { return 3; }
    result.insert("connectButton", true);
    result.insert("selectionMode", catalog->mode());
    result.insert("selectedLocationChangedFromFirst", session->activeId() != catalog->entries().first().id);
    if (!wait([&]() { return notificationTitle == QStringLiteral("Hop"); }, 1500)) { return 3; }
    result.insert("notificationTitle", notificationTitle);
    if (!originalSource.isNull()) {
        // Имитация NAT-программы, сохранившей привязку к исходному адаптеру хоста.
        QUdpSocket socket; socket.setProxy(QNetworkProxy::NoProxy);
        const auto query = QByteArray::fromHex("192a010000010000000000000a686f702d6865616c7468076578616d706c6503636f6d0000010001");
        const bool sent = socket.bind(originalSource, 0) && socket.writeDatagram(query, QHostAddress("198.18.0.1"), 53) == query.size();
        const bool received = sent && wait([&]() { return socket.hasPendingDatagrams(); }, 2500);
        QHostAddress sender; quint16 port = 0; QByteArray answer(4096, Qt::Uninitialized);
        const auto bytes = received ? socket.readDatagram(answer.data(), answer.size(), &sender, &port) : -1;
        result.insert("boundUplinkDnsThroughHop", bytes >= 12 && sender == QHostAddress("198.18.0.1") && port == 53 && answer.left(2) == query.left(2));
        result.insert("boundUplinkDnsSocketError", int(socket.error()));
    }
    if (awg) {
        endpointHealth->checkAll();
        const bool deferred = !endpointHealth->isBusy() && endpointHealth->statuses().value(catalog->entries().first().serverId).toMap().value("state") == "deferred";
        result.insert("awgProbeDeferredWhileConnected", deferred); if (!deferred) { return 3; }
    }
    if (lanPort) {
        const int captured = NetworkUtilities::AdapterIndexTo(lanAddress);
        const bool routed = captured > 0 && captured == originalLanInterface;
        result.insert("lanDirectSystemRoutePreserved", routed);
        const bool direct = request(lanUrl, 204); result.insert("lanDirect", direct);
        if (!routed || !direct) { return 3; }
    }
    bool blocked = false, directAfter = false;
    if (!lanPort) {
        const auto vpnUrl = awg ? QUrl("http://allowed.hop.test:8080/generate_204") : QUrl("https://connectivitycheck.gstatic.com/generate_204");
        const auto blockedDomain = awg ? QStringLiteral("full:allowed.hop.test") : QStringLiteral("full:connectivitycheck.gstatic.com");
        const bool vpn = request(vpnUrl, 204);
        const bool direct = request(QUrl("https://1.0.0.1/cdn-cgi/trace"), 200);
        result.insert("vpnBeforeEdit", vpn); result.insert("directBeforeEdit", direct);
        if (!vpn || !direct) { return 3; }
        if (!awg && !networkTest && idleMode == "monitoring") {
            const auto active = session->activeId();
            const auto sourceId = catalog->serviceInfo(active).value("id").toString();
            const bool member = catalog->marks().value(active).toMap().value("priority").toInt() > 0;
            if (!member) { catalog->toggleLocation(active); }
            catalog->setMode(LocationCatalog::Automatic);
            const auto token = session->token();
            bool refreshed = false, earlySuspend = false, detachedGuardHeld = true;
            int refreshRequests = 0;
            const auto requestObserver = QObject::connect(session, &SelectionSession::subscriptionRefreshRequested, core,
                [&](const QString &, const QString &) {
                    if (++refreshRequests > 1) { detachedGuardHeld &= !connection->isConnected() && guardCount() > 0; }
                });
            const auto refreshObserver = QObject::connect(subscriptions, &HttpSubscriptionsController::sourceRefreshFinished, core,
                [&](const QString &id, bool successful) { if (id == sourceId) { refreshed = successful; } });
            const auto suspendObserver = QObject::connect(session, &SelectionSession::suspendRequested, core, [&](const QString &) {
                const auto state = subscriptions->sourceStatuses().value(sourceId).toMap().value("state").toString();
                earlySuspend |= state != QStringLiteral("ready") && state != QStringLiteral("error");
            });
            const auto removeObservers = qScopeGuard([&]() { QObject::disconnect(refreshObserver); QObject::disconnect(suspendObserver); QObject::disconnect(requestObserver); });
            // Синтетический сигнал деградации; загрузка подписки и повторное подключение настоящие.
            session->setLatencyPolicy(1, 1);
            session->healthResult(token, true, true, true, 2);
            QElapsedTimer delay; delay.start(); wait([&]() { return delay.elapsed() >= 2; }, 100);
            session->healthResult(token, true, true, true, 2);
            const bool held = session->state() == SelectionSession::Refreshing && session->token() == token
                && connection->isConnected() && guardCount() > 0;
            result.insert("refreshBeforeFailoverHeldConnection", held);
            session->setLatencyPolicy(60000, 1500);
            const bool recovered = held && wait([&]() { return refreshed && session->token() != token
                && session->state() == SelectionSession::Healthy && connection->isConnected(); }, 55000);
            result.insert("refreshBeforeFailoverRecovered", recovered && !earlySuspend);
            result.insert("refreshSourceStatus", QJsonObject::fromVariantMap(subscriptions->sourceStatuses().value(sourceId).toMap()));
            result.insert("refreshFinishedSuccessfully", refreshed);
            result.insert("refreshRetryStarted", session->token() != token);
            result.insert("refreshRequests", refreshRequests);
            result.insert("refreshDetachedGuardHeld", detachedGuardHeld);
            catalog->setMode(LocationCatalog::Manual);
            if (!member) { catalog->toggleLocation(active); }
            if (!recovered || earlySuspend || !detachedGuardHeld) { return 3; }
        }
        if (!measureIdle(QStringLiteral("connected")) || !connection->isConnected()) { return 3; }
        if (networkTest) {
            source.write("{\"phase\":\"connected\"}\n"); source.flush();
            const bool response = wait([&]() { return source.canReadLine(); }, 10000);
            const auto responseData = response ? QJsonDocument::fromJson(source.readLine(1024)).object() : QJsonObject();
            const bool restored = responseData.value("restored").toBool() && responseData.value("downObserved").toBool()
                && responseData.value("interruptionMs").toInteger() >= 3500;
            const bool healthy = restored && wait([&]() { return session->state() == SelectionSession::Healthy && connection->isConnected(); }, 35000);
            network.clearConnectionCache();
            const bool recovered = healthy && guardCount() > 0 && request(vpnUrl,204)
                && request(QUrl("https://1.0.0.1/cdn-cgi/trace"),200);
            result.insert("networkRecovered", recovered); if (!recovered) { return 3; }
            source.disconnectFromServer();
        }
        FILETIME created {}, ended {}, kernel {}, user {}; quint64 ticks = 0; QElapsedTimer sample;
        if (GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
            ticks = (quint64(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime + (quint64(user.dwHighDateTime) << 32) + user.dwLowDateTime; sample.start();
        }
        wait([&]() { return sample.isValid() && sample.elapsed() >= 3000; }, 3100);
        if (sample.isValid() && GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
            const auto now = (quint64(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime + (quint64(user.dwHighDateTime) << 32) + user.dwLowDateTime;
            result.insert("connectedCpuPercentOneCore", double(now - ticks) / 100.0 / sample.elapsed());
        }
        PROCESS_MEMORY_COUNTERS_EX memory {}; memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
            result.insert("connectedWorkingSetMiB", double(memory.WorkingSetSize) / 1048576); result.insert("connectedPrivateMiB", double(memory.PrivateUsage) / 1048576);
        }
        if (awg && !networkTest) {
            QNetworkRequest download(QUrl("http://allowed.hop.test:8080/bytes-4m")); download.setTransferTimeout(30000);
            download.setRawHeader("Connection", "close");
            auto *reply = network.get(download); QCryptographicHash hash(QCryptographicHash::Sha256); qint64 received = 0;
            const auto consume = [&]() {
                const auto chunk = reply->readAll(); received += chunk.size(); hash.addData(chunk);
                if (received > 4194304) { reply->abort(); }
            };
            const auto consuming = QObject::connect(reply, &QNetworkReply::readyRead, reply, consume);
            QElapsedTimer transfer; transfer.start();
            const bool done = wait([&]() { return reply->isFinished(); }, 31000); consume();
            QObject::disconnect(consuming);
            result.insert("fixtureTransferWaitFinished", done);
            result.insert("fixtureTransferReplyError", int(reply->error()));
            result.insert("fixtureTransferHttpStatus", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
            if (!done) { reply->abort(); }
            QByteArray pattern(65536, Qt::Uninitialized);
            for (qsizetype index = 0; index < pattern.size(); ++index) { pattern[index] = char(index & 255); }
            QCryptographicHash expected(QCryptographicHash::Sha256);
            for (int index = 0; index < 64; ++index) { expected.addData(pattern); }
            const bool verified = done && reply->error() == QNetworkReply::NoError && received == 4194304 && hash.result() == expected.result();
            result.insert("fixtureTransferVerified", verified); result.insert("fixtureTransferBytes", received);
            result.insert("fixtureTransferElapsedMs", transfer.elapsed());
            result.insert("fixtureThroughputMbps", verified ? double(received) * 8 / (1000 * std::max(qint64(1), transfer.elapsed())) : 0);
            reply->deleteLater(); if (!verified) { return 3; }
        }
        if (idleMode != "monitoring") {
        const auto before = session->token();
        if (!click("hopRoutesButton") || !addRule(0, 2, blockedDomain)) { return 3; }
        result.insert("stage", "policy-reconnect");
        if (!wait([&]() { return session->token() != before && session->state() == SelectionSession::Healthy && connection->isConnected(); }, 35000)) { return 3; }
        result.insert("reconnectedAfterEdit", true);
        network.clearConnectionCache();
        blocked = request(vpnUrl, 0);
        directAfter = request(QUrl("https://1.0.0.1/cdn-cgi/trace"), 200);
        result.insert("domainBlockedAfterEdit", blocked); result.insert("directAfterEdit", directAfter);
        if (orderTest) {
            const auto orderToken = session->token(); const auto snapshot = policy->snapshot();
            int publications = 0, suspensions = 0;
            const auto publishing = QObject::connect(policy, &TrafficPolicyController::policyChanged, core, [&]() { ++publications; });
            const auto suspending = QObject::connect(session, &SelectionSession::suspendRequested, core, [&](const QString &) { ++suspensions; });
            const auto disconnectOrderObservers = qScopeGuard([&]() { QObject::disconnect(publishing); QObject::disconnect(suspending); });
            if (!click("hopRoutesUp_2") || !click("hopRoutesUp_1")) { return 3; }
            QElapsedTimer settle; settle.start(); wait([&]() { return settle.elapsed() >= 1000; }, 1100);
            const bool stable = policy->orderPending() && !policy->busy() && policy->snapshot() == snapshot
                && session->token() == orderToken && connection->isConnected() && publications == 0 && suspensions == 0;
            result.insert("ruleOrderNoReconnect", stable); if (!stable) { return 3; }
            if (!click("hopRoutesOrderApply") || !wait([&]() { return !policy->busy() && !policy->orderPending()
                && session->token() != orderToken && session->state() == SelectionSession::Healthy && connection->isConnected(); }, 25000)) { return 3; }
            const bool once = publications == 1 && suspensions == 1;
            result.insert("ruleOrderAppliedOnce", once); if (!once) { return 3; }
            network.clearConnectionCache();
            const bool routed = request(vpnUrl, 0) && request(QUrl("https://1.0.0.1/cdn-cgi/trace"), 200);
            result.insert("ruleOrderRoutesVerified", routed); if (!routed) { return 3; }
        }
        }
    }
    if (lanPort) {
        result.insert("isolatedLanScenario", true);
        result.insert("stage", "lan-private-block");
        auto rules = QJsonArray::fromVariantList(policy->rules());
        auto privateRule = rules.first().toObject(); privateRule.insert(QStringLiteral("target"), QStringLiteral("block")); rules[0] = privateRule;
        const auto token = session->token();
        if (!policy->apply(QJsonObject { { "default", "vpn" }, { "rules", rules } }, true)
            || !wait([&]() { return !policy->busy() && session->token() != token && session->state() == SelectionSession::Healthy && connection->isConnected(); }, 25000)) { return 3; }
        result.insert("reconnectedAfterEdit", true);
        network.clearConnectionCache(); const bool denied = request(lanUrl, 0);
        result.insert("lanBlockRouteCaptured", NetworkUtilities::AdapterIndexTo(lanAddress) != originalLanInterface);
        result.insert("lanBlockedAfterEdit", denied); if (!denied) { return 3; }
    }
    core->pageController()->goToPageHome();
    QElapsedTimer disconnectTime; disconnectTime.start();
    if (!wait([&]() { return item("hopConnectButton"); }, 3000) || !click("hopConnectButton")) { return 3; }
    passed = wait([&]() { return !session->running() && !connection->isConnected() && !connectionUi->isConnectionInProgress() && guardCount() == 0; }, 20000);
    result.insert("manualDisconnectMs",disconnectTime.elapsed());
    result.insert("sessionRunningAfterDisconnect",session->running());
    result.insert("uiProgressAfterDisconnect",connectionUi->isConnectionInProgress());
    result.insert("guardAfterDisconnect",guardCount());
    result.insert("disconnectButton", passed);
    const bool protectionUiCleared = passed && wait([&]() { return connectionUi->protectionServiceAvailable() && !connectionUi->trafficProtectionActive(); }, 5000);
    result.insert("protectionUiCleared", protectionUiCleared);
    if (lanPort) {
        const bool restored = passed && NetworkUtilities::AdapterIndexTo(lanAddress) == originalLanInterface;
        result.insert("lanOriginalRouteRestored", restored); if (!restored) { return 3; }
        network.clearConnectionCache();
        const bool reachable = request(lanUrl, 204);
        result.insert("lanAfterDisconnect", reachable); if (!reachable) { return 3; }
    }
    return passed && protectionUiCleared && (lanPort || idleMode == "monitoring" || (blocked && directAfter)) && noConsole && warnings == 0 ? 0 : 3;
}
}
#endif
