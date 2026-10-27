#include <QApplication>
#include <QDir>
#include <QDateTime>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QUuid>

#include "utils/testCoreController.h"
#include "ui/utils/pageEnum.h"
#include "logger.h"
#include "core/locations/locationCatalog.h"
#include "core/routing/trafficPolicyController.h"
#include "core/locations/endpointHealthController.h"
#include "core/locations/locationHistoryStore.h"
#include "core/locations/locationCoordinator.h"
#include "core/locations/locationSearchModel.h"
#include "core/locations/refreshPreferences.h"
#include "ui/controllers/connectionsPageController.h"
#include "ui/controllers/qml/pageController.h"
#include <QMenu>
#include <QThreadPool>
#include "hopDesktopStyle.h"
#include "ui/utils/hopLanguage.h"
#include "ui/utils/systemTrayNotificationHandler.h"
#include <QTranslator>
#include <QWheelEvent>
#include <QJSEngine>
#include <QFile>

namespace {
QQuickItem *findVisualItem(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name) { return item; }
    for (auto *child : item->childItems()) {
        if (auto *found = findVisualItem(child, name)) { return found; }
    }
    return nullptr;
}
}

class TestHopDashboard : public QObject {
    Q_OBJECT
private slots:
    void smoothLatencyCurveKeepsExtremaAndGaps()
    {
        QFile source(":/ui/qml/Components/HopLatencyCurve.js"); QVERIFY(source.open(QIODevice::ReadOnly));
        QJSEngine engine;
        auto script = QString::fromUtf8(source.readAll()); script.remove(".pragma library");
        QVERIFY(!engine.evaluate(script).isError());
        const auto check = engine.evaluate(R"(
(function() {
    const cases = [
        [{x:0,y:10},{x:1,y:10},{x:2,y:10}],
        [{x:0,y:0},{x:1,y:100},{x:20,y:101},{x:21,y:0}],
        [{x:0,y:10},{x:5,y:30},{x:6,y:5},{x:10,y:40}]
    ];
    let curved = false;
    for (const points of cases) {
        const curves = smooth(points);
        if (curves.length !== points.length-1) return false;
        for (let i=0; i<curves.length; ++i) {
            const a=points[i], b=curves[i], next=points[i+1];
            if (b.x !== next.x || b.y !== next.y) return false;
            for (let step=0; step<=100; ++step) {
                const t=step/100, s=1-t;
                const y=s*s*s*a.y+3*s*s*t*b.y1+3*s*t*t*b.y2+t*t*t*b.y;
                if (y < Math.min(a.y,b.y)-1e-8 || y > Math.max(a.y,b.y)+1e-8) return false;
                const linear=a.y+(b.y-a.y)*t;
                curved = curved || Math.abs(y-linear)>0.1;
            }
        }
    }
    const samples=[0,1,2,3,4,20,21].map(function(time,i) {return {at:time*60000,latencyMs:100,ok:i!==2};});
    const chart=build(samples,50,10,500,180,100);
    return curved && chart.points.length===samples.length && chart.segments.length===3
        && chart.segments.every(function(s) {return s.points.length===2;});
})()
)");
        QVERIFY2(!check.isError(), qPrintable(check.toString())); QVERIFY(check.toBool());
    }

    void latencyChartAdaptsWindowAndScaleWithoutHidingPeaks()
    {
        QFile source(":/ui/qml/Components/HopLatencyCurve.js"); QVERIFY(source.open(QIODevice::ReadOnly));
        QJSEngine engine; auto script = QString::fromUtf8(source.readAll()); script.remove(".pragma library");
        QVERIFY(!engine.evaluate(script).isError());
        const auto check = engine.evaluate(R"(
(function() {
    const points=[{at:60000,from:0,to:120000,bucket:0,latencyMs:60,minimum:59,maximum:100,ok:true,count:20,failures:2},
                  {at:180000,from:120000,to:240000,bucket:1,latencyMs:300,minimum:100,maximum:7282,ok:true,count:20,failures:1},
                  {at:660000,from:600000,to:720000,bucket:5,latencyMs:150,minimum:80,maximum:200,ok:true,count:20,failures:0}];
    const graph=build(points,50,10,600,200,7282,0,900000);
    const ordinary=build([{at:0,latencyMs:100,ok:true},{at:1,latencyMs:200,ok:true}],50,10,600,200,200);
    return graph.start===0 && graph.span===900000 && graph.logarithmic && graph.ceiling>=7282
        && graph.segments.length===2 && graph.points[1].high<graph.points[1].y
        && graph.points[0].y-graph.points[1].y>20 && graph.points[0].failureFraction===0.1
        && graph.points[0].from===50 && graph.ticks[0].value===0 && !ordinary.logarithmic;
})()
)");
        QVERIFY2(!check.isError(), qPrintable(check.toString())); QVERIFY(check.toBool());
    }

    void denseStatisticsAdaptAcrossAllPeriods()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto end = QDateTime::currentMSecsSinceEpoch();
        const auto start = end - hop::LocationHealthHistory::retentionMs + 60000;
        int total = 0;
        {
            hop::LocationHistoryStore seed(directory.path());
            for (int index = 0; index < 12134; ++index) {
                if ((index >= 2000 && index < 3500) || (index >= 8000 && index < 9000)) continue;
                const int latency = index == 6000 ? 7282 : index % 41 == 0 ? -1 : 59 + (index / 9) % 300;
                seed.record("weekly", latency, start + (end - start) * index / 12133); ++total;
                if (total % 1000 == 0) { seed.flush(); QTRY_VERIFY(!seed.busy()); }
            }
            seed.flush(); QTRY_VERIFY(!seed.busy());
        }
        SecureQSettings settings("Hop-Adaptive-Stats", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings);
        hop::EndpointHealthController health(&servers, nullptr, {}, {}, directory.path());
        QQmlApplicationEngine engine; engine.addImportPath("qrc:/ui/qml/Modules/");
        engine.rootContext()->setContextProperty("History", &health);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import Style 1.0
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true; width: 1000; height: 740; color: HopStyle.background
    HopLocationStatsDialog {
        id: statistics; anchors.centerIn: parent; controller: History; locationId: "weekly"
        info: ({name:"BlancVPN · Amsterdam",protocol:"VLESS / Reality"})
    }
    Component.onCompleted: statistics.open()
})", QUrl("qrc:/hop-adaptive-statistics-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *dialog = window->findChild<QObject *>("hopLocationStatsDialog"); QVERIFY(dialog);
        auto *chart = findVisualItem(window->contentItem(), "hopLocationLatencyGraph"); QVERIFY(chart);
        auto *period = window->findChild<QObject *>("hopLocationStatsPeriod"); QVERIFY(period);
        QTRY_COMPARE(dialog->property("statistics").toMap().value("count").toInt(), total);
        for (int index : {4, 3, 2, 1, 0}) {
            QVERIFY(period->setProperty("currentIndex", index));
            QVERIFY(QMetaObject::invokeMethod(period, "activated", Q_ARG(int, index)));
            const QList<int> minutes{15, 60, 360, 1440, 10080};
            QCOMPARE(chart->property("windowEnd").toDouble() - chart->property("windowStart").toDouble(), double(minutes[index]) * 60000);
            const auto statistics = dialog->property("statistics").toMap();
            QVERIFY(statistics.value("count").toInt() > 0);
            QVERIFY(statistics.value("samples").toList().size() <= 240);
            QCOMPARE(statistics.value("aggregated").toBool(), index >= 2);
            const auto geometry = chart->property("geometry").value<QJSValue>().toVariant().toMap();
            QCOMPARE(geometry.value("logarithmic").toBool(), index == 4);
            if (index == 4) QCOMPARE(statistics.value("maximum").toInt(), 7282);
            QTest::qWait(200);
            const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
            if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath(QString("adaptive-statistics-%1.png").arg(index)))); }
        }
        QCOMPARE(warnings.count(), 0);
    }

    void smoothLatencyGraphHasScaleAndSampleInspection()
    {
        QQmlApplicationEngine engine; engine.addImportPath("qrc:/ui/qml/Modules/");
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true; width: 1000; height: 710
    HopLocationStatsDialog { id: statistics; anchors.centerIn: parent; info: ({name:"Amsterdam",protocol:"VLESS / Reality"}) }
    Component.onCompleted: {
        statistics.open()
        const now=Date.now(), points=[]
        const latencies=[240,310,290,600,420,380,850,430,-1,320,480,340,250]
        for(let i=0;i<latencies.length;++i) points.push({at:now-(latencies.length-1-i)*60000,latencyMs:latencies[i],ok:latencies[i]>=0})
        statistics.statistics=({samples:points,count:13,failures:1,minimum:240,average:411,maximum:850})
    }
})", QUrl("qrc:/hop-smooth-chart-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *chart = findVisualItem(window->contentItem(), "hopLocationLatencyGraph"); QVERIFY(chart);
        QTRY_VERIFY(chart->height() >= 90);
        QTest::qWait(200);
        auto *area = findVisualItem(window->contentItem(), "hopLatencyChartHover"); QVERIFY(area);
        const auto cursor = area->mapToScene(QPointF(chart->property("plotLeft").toDouble() + chart->property("plotWidth").toDouble()/2, area->height()/2)).toPoint();
        QTest::mouseMove(window, cursor, 100);
        QTRY_VERIFY(chart->property("selectedIndex").toInt() >= 0);
        auto *sample = findVisualItem(window->contentItem(), "hopLatencyChartSample"); QVERIFY(sample);
        QVERIFY(!sample->property("text").toString().isEmpty());
        QTest::qWait(200);
        const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath("smooth-location-statistics.png"))); }
        QCOMPARE(warnings.count(), 0);
    }

    void cancellationIsVisibleUntilCleanupAcknowledged()
    {
        SecureQSettings settings("Hop-Cancel-Feedback", "memory", nullptr, true, false);
        QQmlApplicationEngine engine; engine.addImportPath("qrc:/ui/qml/Modules/");
        engine.rootContext()->setContextProperty("Debug", &Logger::Instance());
        engine.rootContext()->setContextProperty("IsMacOsNeBuild", false);
        engine.rootContext()->setContextProperty("IsPlayBuild", false);
        const auto vpn = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        TestCoreController core(vpn, &settings, &engine);
        const auto cleanup = qScopeGuard([&]() { qDeleteAll(engine.rootObjects()); QThreadPool::globalInstance()->waitForDone(5000); });
        auto *ui = core.m_connectionUiController;
        auto *transport = core.findChild<ConnectionController *>(); QVERIFY(transport);
        // Ответ службы управляется тестом; реальные VPN и сетевые настройки не затрагиваются.
        QObject::disconnect(transport, &ConnectionController::closeConnectionRequested, vpn.data(), &VpnConnection::disconnectFromVpn);
        ui->setLocationCoordinator(nullptr);
        QSignalSpy requests(transport, &ConnectionController::closeConnectionRequested);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Pages2"
ApplicationWindow {
    visible: true; width: 1000; height: 710
    Item { objectName: "defaultFocusItem" }
    PageHomeHop { anchors.fill: parent }
})", QUrl("qrc:/hop-cancel-feedback-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        const auto visual = [&](const QString &name) { return findVisualItem(window->contentItem(), name); };
        QTRY_VERIFY(visual("hopDashboard"));
        auto *dashboard = visual("hopDashboard");
        QVERIFY(dashboard->setProperty("selectedServerId", "synthetic"));
        QVERIFY(dashboard->setProperty("selectedServerName", "Test subscription · Test location"));
        auto *button = visual("hopConnectButton"); auto *status = visual("hopConnectionStatus");
        QVERIFY(button && status);
        for (int language : {0, 2, 1}) {
            core.m_languageUiController->changeLanguage(static_cast<LanguageSettings::AvailableLanguageEnum>(language));
            const QStringList cancelling {"Cancelling…", QString::fromUtf8("Отменяется…"), QString::fromUtf8("正在取消…")};
            const QStringList statusText {"Cancelling connection…", QString::fromUtf8("Отмена подключения…"), QString::fromUtf8("正在取消连接…")};
            const QStringList disconnected {"Disconnected", QString::fromUtf8("Отключено"), QString::fromUtf8("未连接")};
            for (auto state : {Vpn::Connecting, Vpn::Reconnecting}) {
                ui->onConnectionStateChanged(state);
                QTRY_VERIFY(button->isEnabled());
                const int before = requests.count();
                QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
                QTRY_COMPARE(requests.count(), before + 1);
                QVERIFY(ui->isCancelling()); QVERIFY(ui->isDisconnecting());
                QTRY_COMPARE(button->property("text").toString(), cancelling[language]);
                QTRY_COMPARE(status->property("text").toString(), statusText[language]);
                QVERIFY(!button->isEnabled());
                QVERIFY(ui->isConnectionInProgress());
                ui->toggleConnection(); ui->closeConnection();
                QCOMPARE(requests.count(), before + 1);
                // Даже до и после запоздалого уведомления о запуске показывается отмена.
                ui->onConnectionStateChanged(Vpn::Connecting);
                QCOMPARE(status->property("text").toString(), statusText[language]);
                ui->onConnectionStateChanged(Vpn::Disconnecting);
                QCOMPARE(button->property("text").toString(), cancelling[language]);
                QTest::qWait(100);
                const auto screenshot = qEnvironmentVariable("HOP_CANCEL_SCREENSHOT");
                if (!screenshot.isEmpty()) { QVERIFY(window->grabWindow().save(screenshot + QString::number(language) + ".png")); }
                ui->onConnectionStateChanged(Vpn::Disconnected);
                QVERIFY(!ui->isCancelling()); QVERIFY(!ui->isDisconnecting());
                QTRY_COMPARE(status->property("text").toString(), disconnected[language]);
                QTRY_VERIFY(button->isEnabled());
            }
        }
        // Снятие защиты до запуска транспорта также остаётся в процессе до ACK.
        ui->closeConnection();
        QVERIFY(ui->isDisconnecting()); QVERIFY(!ui->isCancelling());
        ui->onTranslationsUpdated();
        QVERIFY(ui->isDisconnecting());
        QCOMPARE(status->property("text").toString(), QString::fromUtf8("Отключение…"));
        QCOMPARE(button->property("text").toString(), QString::fromUtf8("Отключается…"));
        QVERIFY(!button->isEnabled());
        ui->onConnectionStateChanged(Vpn::Disconnected);
        QCOMPARE(warnings.count(), 0);
    }

    void scrollingAndLiveSidebarDialogTranslations()
    {
        SecureQSettings settings("Hop-Scroll-Language", "memory", nullptr, true, false);
        QQmlApplicationEngine engine; engine.addImportPath("qrc:/ui/qml/Modules/");
        engine.rootContext()->setContextProperty("Debug", &Logger::Instance());
        engine.rootContext()->setContextProperty("IsMacOsNeBuild", false);
        engine.rootContext()->setContextProperty("IsPlayBuild", false);
        const auto vpn = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        TestCoreController core(vpn, &settings, &engine);
        const auto cleanup = qScopeGuard([&]() { qDeleteAll(engine.rootObjects()); QThreadPool::globalInstance()->waitForDone(5000); });
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import Style 1.0
import "qrc:/ui/qml/Pages2"
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true; width: 1000; height: 600; color: HopStyle.background
    palette.text: HopStyle.text; palette.windowText: HopStyle.text; palette.buttonText: HopStyle.text
    Item { objectName: "defaultFocusItem" }
    PageStart { anchors.fill: parent }
    HopGeoDataDialog { objectName: "languageRoutingDialog"; anchors.centerIn: parent }
})", QUrl("qrc:/hop-scroll-language-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        const auto visual = [&](const QString &name) { return findVisualItem(window->contentItem(), name); };
        QTRY_VERIFY(visual("hopSettingsTab"));
        QVERIFY(QMetaObject::invokeMethod(visual("hopSettingsTab"), "clicked"));
        QTRY_VERIFY(visual("hopSettingsScrollBar"));
        auto *bar = visual("hopSettingsScrollBar"); auto *scroll = visual("hopSettingsScroll"); QVERIFY(scroll);
        auto *flickable = scroll->property("contentItem").value<QObject *>(); QVERIFY(flickable);
        QTRY_VERIFY(bar->height() > 400);
        QCOMPARE(bar->parentItem(), scroll);
        QCOMPARE(bar->x() + bar->width(), scroll->width());
        QTRY_VERIFY(bar->isVisible());
        const auto thumb = bar->mapToScene(QPointF(bar->width()/2, bar->height()*bar->property("visualSize").toDouble()/2)).toPoint();
        const auto bottom = bar->mapToScene(QPointF(bar->width()/2, bar->height()*0.85)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, thumb);
        QTest::mouseMove(window, bottom, 100);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, bottom);
        QTRY_VERIFY(flickable->property("contentY").toDouble() > 100);
        const auto scrolled = flickable->property("contentY").toDouble();
        const QPointF cursor = scroll->mapToScene(QPointF(scroll->width()/2, scroll->height()/2));
        QWheelEvent wheel(cursor, window->mapToGlobal(cursor.toPoint()), QPoint(), QPoint(0, 1200),
            Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(flickable->property("contentY").toDouble() < scrolled);
        auto *dialog = window->findChild<QObject *>("languageRoutingDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(visual("hopDialogCloseButton"));
        for (int index : {0, 2, 1}) {
            core.m_languageUiController->changeLanguage(static_cast<LanguageSettings::AvailableLanguageEnum>(index));
            const QStringList connections {"Connections", QString::fromUtf8("Подключения"), QString::fromUtf8("连接")};
            const QStringList routes {"Routes", QString::fromUtf8("Маршруты"), QString::fromUtf8("路由")};
            const QStringList automatic {"Auto-switching", QString::fromUtf8("Автопереключение"), QString::fromUtf8("自动切换")};
            const QStringList close {"Close", QString::fromUtf8("Закрыть"), QString::fromUtf8("关闭")};
            QCOMPARE(visual("hopHomeTab")->property("text").toString(), connections[index]);
            QCOMPARE(visual("hopRoutesButton")->property("text").toString(), routes[index]);
            QCOMPARE(visual("hopLocationPolicyButton")->property("text").toString(), automatic[index]);
            QTest::qWait(150);
            QTRY_COMPARE(visual("hopDialogCloseButton")->property("displayText").toString(), close[index]);
            auto *label = visual("hopDialogCloseButton")->property("contentItem").value<QObject *>(); QVERIFY(label);
            QCOMPARE(label->property("text").toString(), close[index]);
            const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
            if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath(QStringLiteral("sidebar-routing-language-%1.png").arg(index)))); }
        }
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QCOMPARE(warnings.count(), 0);
    }
    void systemUiLanguageAndSavedPreference()
    {
        QCOMPARE(hop::systemUiLanguage({"ru-UA"}).language(), QLocale::Russian);
        QCOMPARE(hop::systemUiLanguage({"en-GB"}).language(), QLocale::English);
        QCOMPARE(hop::systemUiLanguage({"zh-TW"}).name(), QStringLiteral("zh_CN"));
        QCOMPARE(hop::systemUiLanguage({"de-DE", "ru-RU"}).language(), QLocale::English);
        QCOMPARE(hop::systemUiLanguage({"fr-FR"}).language(), QLocale::English);
        SecureQSettings settings("Hop-Language-Preference", "memory", nullptr, true, false);
        SecureAppSettingsRepository repository(&settings);
        QCOMPARE(repository.getAppLanguage(), hop::systemUiLanguage());
        repository.setAppLanguage(QLocale(QLocale::Chinese));
        SecureAppSettingsRepository reloaded(&settings);
        QCOMPARE(reloaded.getAppLanguage().name(), QStringLiteral("zh_CN"));
        repository.setAppLanguage(QLocale(QLocale::French));
        QCOMPARE(reloaded.getAppLanguage().language(), QLocale::English);
    }
    void trayConnectionDetailsAndTranslation()
    {
        const QString originalName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("Hop");
        const auto restoreName = qScopeGuard([&]() { QCoreApplication::setApplicationName(originalName); });
        QObject owner;
        SystemTrayNotificationHandler tray(&owner);
        auto *trayIcon = owner.findChild<QSystemTrayIcon *>(); QVERIFY(trayIcon);
        const auto mint = QIcon(":/images/hop/hop-disconnected.ico").pixmap(32, 32).toImage();
        const auto orange = QIcon(":/images/hop/hop.ico").pixmap(32, 32).toImage();
        QVERIFY(!mint.isNull()); QVERIFY(!orange.isNull()); QVERIFY(mint != orange);
        QCOMPARE(trayIcon->icon().availableSizes().size(), 15);
        QCOMPARE(trayIcon->icon().pixmap(32, 32).toImage(), mint);
        for (auto state : {Vpn::Preparing, Vpn::Connecting, Vpn::Reconnecting, Vpn::Disconnecting,
                           Vpn::Disconnected, Vpn::Error, Vpn::Unknown}) {
            tray.setConnectionState(Vpn::Connected);
            QCOMPARE(trayIcon->icon().pixmap(32, 32).toImage(), orange);
            tray.setConnectionState(state);
            QCOMPARE(trayIcon->icon().pixmap(32, 32).toImage(), mint);
        }
        tray.setConnectionDetails("BlancVPN", "Amsterdam", 203);
        tray.setConnectionState(Vpn::Connected);
        QVERIFY(tray.connectionToolTip().contains("BlancVPN"));
        QVERIFY(tray.connectionToolTip().contains("Amsterdam"));
        QVERIFY(tray.connectionToolTip().contains("203"));
        tray.setConnectionDetails("Amnezia Premium", "Netherlands", 150);
        QVERIFY(tray.connectionToolTip().contains("150"));
        QVERIFY(!tray.connectionToolTip().contains("203"));
        QTranslator translator;
        QVERIFY(translator.load(":/translations/hop_en.qm"));
        QCoreApplication::installTranslator(&translator);
        tray.onTranslationsUpdated();
        QVERIFY(tray.connectionToolTip().contains("Response time: 150 ms"));
        tray.setConnectionDetails(QString(100, 'S'), QString(100, 'L'), -1);
        QVERIFY(tray.connectionToolTip().contains("Response time: no data"));
        QVERIFY(tray.connectionToolTip().size() <= 127);
        tray.setConnectionState(Vpn::Disconnected);
        QVERIFY(tray.connectionToolTip().contains("Disconnected"));
        QVERIFY(!tray.connectionToolTip().contains("Subscription:"));
        QCoreApplication::removeTranslator(&translator);
    }
    void languageSettingsRetranslateImmediately()
    {
        QVERIFY(QFontMetrics(QFont("Hop CJK UI")).inFontUcs4(0x8BBE));
        SecureQSettings settings("Hop-Language-UI", "memory", nullptr, true, false);
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty("Debug", &Logger::Instance());
        engine.rootContext()->setContextProperty("IsMacOsNeBuild", false);
        engine.rootContext()->setContextProperty("IsPlayBuild", false);
        const auto vpn = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        TestCoreController core(vpn, &settings, &engine);
        const auto cleanup = qScopeGuard([&]() { qDeleteAll(engine.rootObjects()); QThreadPool::globalInstance()->waitForDone(5000); });
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import Style 1.0
import "qrc:/ui/qml/Pages2"
ApplicationWindow {
    visible: true; width: 900; height: 980
    color: HopStyle.background
    palette.windowText: HopStyle.text
    palette.text: HopStyle.text
    palette.buttonText: HopStyle.text
    palette.placeholderText: HopStyle.muted
    PageSettingsHop { anchors.fill: parent }
})", QUrl("qrc:/hop-language-settings-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *language = window->findChild<QObject *>("hopApplicationLanguage"); QVERIFY(language);
        QCOMPARE(language->property("count").toInt(), 3);
        const QStringList expected {"Settings", QString::fromUtf8("Настройки"), QString::fromUtf8("设置")};
        for (int index : {0, 2, 1}) {
            QVERIFY(language->setProperty("currentIndex", index));
            QVERIFY(QMetaObject::invokeMethod(language, "activated", Q_ARG(int, index)));
            QCOMPARE(core.m_languageUiController->getCurrentLanguageIndex(), index);
            QCOMPARE(QCoreApplication::translate("PageSettingsHop", "Настройки"), expected[index]);
            bool titleFound = false;
            for (auto *item : window->findChildren<QObject *>()) {
                if (item->property("text").toString() == expected[index]) { titleFound = true; break; }
            }
            QVERIFY(titleFound);
            QCOMPARE(language->property("height").toInt(), 36);
            QTest::qWait(100);
            const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
            if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath(QStringLiteral("settings-language-%1.png").arg(index)))); }
        }
        QCOMPARE(warnings.count(), 0);
    }
    void locationStatisticsUsesMeasuredHistory_data() {
        QTest::addColumn<bool>("persistent");
        QTest::newRow("memory") << false;
        QTest::newRow("restored-from-disk") << true;
    }
    void locationStatisticsUsesMeasuredHistory() {
        QFETCH(bool, persistent);
        QTemporaryDir historyDirectory;
        SecureQSettings settings("Hop-Stats-UI", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings);
        if (persistent) {
            hop::EndpointHealthController previous(&servers, nullptr, {}, {}, historyDirectory.path());
            for (int latency : {90, 150, -1, 70}) { previous.recordSample("location", latency); QTest::qWait(10); }
        }
        hop::EndpointHealthController health(&servers, nullptr, {}, {}, persistent ? historyDirectory.path() : QString());
        if (!persistent) {
            for (int latency : {90, 150, -1, 70}) { health.recordSample("location", latency); QTest::qWait(10); }
        }
        QQmlApplicationEngine engine; engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty("History", &health);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true; width: 800; height: 680
    HopLocationStatsDialog {
        id: statistics; anchors.centerIn: parent; controller: History
        locationId: "location"; info: ({name: "Amsterdam", protocol: "VLESS / Reality"})
        checkAvailable: true
    }
    Component.onCompleted: statistics.open()
})", QUrl("qrc:/hop-location-statistics-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *dialog = window->findChild<QObject *>("hopLocationStatsDialog"); QVERIFY(dialog);
        QTRY_COMPARE(dialog->property("statistics").toMap().value("count").toInt(), 4);
        QCOMPARE(dialog->property("statistics").toMap().value("failures").toInt(), 1);
        QVERIFY(findVisualItem(window->contentItem(), "hopLocationLatencyGraph"));
        auto *period = window->findChild<QObject *>("hopLocationStatsPeriod"); QVERIFY(period);
        QVERIFY(period->setProperty("currentIndex", 2));
        QVERIFY(QMetaObject::invokeMethod(period, "activated", Q_ARG(int, 2)));
        health.recordSample("location", 50);
        QTRY_COMPARE(dialog->property("statistics").toMap().value("count").toInt(), 5);
        QCOMPARE(dialog->property("statistics").toMap().value("minimum").toInt(), 50);
        QTest::qWait(150);
        const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath("native-location-statistics.png"))); }
        QCOMPARE(warnings.count(), 0);
    }
    void sidebarSearchMenusAndRefreshSettings()
    {
        SecureQSettings settings("Hop-Sidebar", "memory", nullptr, true, false);
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty("Debug", &Logger::Instance());
        engine.rootContext()->setContextProperty("IsMacOsNeBuild", false);
        engine.rootContext()->setContextProperty("IsPlayBuild", false);
        const auto vpn = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        TestCoreController core(vpn, &settings, &engine);
        const auto cleanup = qScopeGuard([&]() { qDeleteAll(engine.rootObjects()); QThreadPool::globalInstance()->waitForDone(5000); });
        for (const auto &name : { QStringLiteral("First"), QStringLiteral("Second") }) {
            auto config = core.m_importCoreController->extractConfigFromData(
                QStringLiteral("vless://00000000-0000-4000-8000-000000000001@192.0.2.1:443?security=none&type=tcp#") + name).config;
            config.insert("hopSubscription", QJsonObject{{"sourceId", name}, {"sourceName", name},
                {"sourceUrl", "https://example.invalid/" + name}, {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
            core.m_serversRepository->addServer(name, config, serverConfigUtils::ConfigType::Native);
        }
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Pages2"
ApplicationWindow {
    visible: true; width: 1000; height: 760
    Item { objectName: "defaultFocusItem" }
    PageStart { anchors.fill: parent }
})", QUrl("qrc:/hop-sidebar-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        const auto visual = [&](const QString &name) { return findVisualItem(window->contentItem(), name); };
        const auto click = [&](const QString &name) {
            auto *item = visual(name);
            if (!item || !item->isVisible() || !item->isEnabled()) { return false; }
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
            return true;
        };
        const auto capture = [&](const QString &name) {
            const auto path = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
            if (!path.isEmpty()) { window->grabWindow().save(QDir(path).filePath(name + ".png")); }
        };
        QTRY_VERIFY(visual("hopLocationSearch"));
        auto *sidebar = visual("hopSidebar"); QVERIFY(sidebar);
        QCOMPARE(sidebar->x(), 0.0); QVERIFY(sidebar->height() > 500);
        QVERIFY(click("hopLocationSearch"));
        QTest::keyClick(window, Qt::Key_S); QTest::keyClick(window, Qt::Key_E); QTest::keyClick(window, Qt::Key_C);
        auto *search = core.findChild<hop::LocationSearchModel *>(); QVERIFY(search);
        QTRY_COMPARE(search->rowCount(), 1);
        QTRY_VERIFY(visual("hopSelectServer_0")); QVERIFY(click("hopSelectServer_0"));
        QCOMPARE(core.m_serversRepository->defaultServerId(), QStringLiteral("Second"));
        QVERIFY(click("hopDetailsButton_0"));
        QTRY_VERIFY(visual("hopLocationStats_0") && visual("hopLocationStats_0")->isVisible());
        QVERIFY(!visual("hopLocationInfo_0"));
        capture("sidebar-location-menu");
        QVERIFY(click("hopLocationStats_0"));
        auto *dialog = window->findChild<QObject *>("hopLocationStatsDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        auto *sectionButton = visual("hopServiceActions_Second");
        auto *locationButton = visual("hopDetailsButton_0"); QVERIFY(sectionButton && locationButton);
        QCOMPARE(sectionButton->mapToScene(QPointF()).x(), locationButton->mapToScene(QPointF()).x());
        auto *header = visual("hopServiceHeader_Second");
        auto *serviceStatus = visual("hopServiceStatus_Second"); QVERIFY(serviceStatus);
        QVERIFY(serviceStatus->property("text").toString().startsWith(QStringLiteral("Обновлено")));
        auto *firstLocation = visual("hopLocationRow_0"); QVERIFY(header && firstLocation);
        QCOMPARE(firstLocation->mapToScene(QPointF()).y() - header->mapToScene(QPointF(0, header->height())).y(), 8.0);
        QVERIFY(click("hopServiceActions_Second"));
        QTRY_VERIFY(visual("hopServiceInfo_Second") && visual("hopServiceInfo_Second")->isVisible());
        QVERIFY(visual("hopServiceRefresh_Second")); QVERIFY(visual("hopServiceRefresh_Second")->isEnabled());
        QVERIFY(visual("hopServiceRemove_Second")); QVERIFY(visual("hopServiceRemove_Second")->isEnabled());
        capture("sidebar-service-menu");
        QVERIFY(click("hopServiceInfo_Second"));
        auto *service = window->findChild<QObject *>("hopServiceDialog"); QVERIFY(service);
        QTRY_VERIFY(service->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(service, "close"));
        QVERIFY(click("hopLocationPolicyButton"));
        QTRY_VERIFY(visual("hopSelectionMode"));
        QCOMPARE(visual("hopLatencyDurationLabel")->property("text").toString(), QString::fromUtf8("Длительность превышения, с"));
        QCOMPARE(visual("hopLatencyThresholdLabel")->property("text").toString(), QString::fromUtf8("Порог задержки, мс"));
        capture("sidebar-auto-switch");
        QVERIFY(click("hopSettingsTab"));
        QTRY_VERIFY(visual("hopSubscriptionInterval"));
        auto *preferences = core.findChild<hop::RefreshPreferences *>(); QVERIFY(preferences);
        QCOMPARE(preferences->subscriptionMinutes(), 360); QCOMPARE(preferences->healthMinutes(), 0);
        auto *subscription = visual("hopSubscriptionInterval");
        QCOMPARE(subscription->property("currentValue").toInt(), 360);
        QCOMPARE(visual("hopHealthInterval")->property("currentValue").toInt(), 0);
        subscription->setProperty("currentIndex", 3);
        QVERIFY(QMetaObject::invokeMethod(subscription, "activated", Q_ARG(int, 3)));
        QCOMPARE(preferences->subscriptionMinutes(), 60);
        QCOMPARE(settings.value("Conf/hopSubscriptionMinutes").toInt(), 60);
        auto *health = visual("hopHealthInterval"); QVERIFY(health);
        health->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(health, "activated", Q_ARG(int, 2)));
        QCOMPARE(preferences->healthMinutes(), 5);
        QCOMPARE(settings.value("Conf/hopHealthMinutes").toInt(), 5);
        auto *scope = visual("hopHealthScope"); QVERIFY(scope);
        scope->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, 2)));
        QCOMPARE(preferences->healthScope(), 2);
        preferences->setLatencySeconds(90); preferences->setLatencyThreshold(500);
        hop::RefreshPreferences restored(&settings, core.findChild<hop::HttpSubscriptionsController *>(),
            core.findChild<hop::LocationCoordinator *>(), core.findChild<hop::EndpointHealthController *>());
        QCOMPARE(restored.subscriptionMinutes(), 60); QCOMPARE(restored.healthMinutes(), 5);
        QCOMPARE(restored.healthScope(), 2); QCOMPARE(restored.latencySeconds(), 90); QCOMPARE(restored.latencyThreshold(), 500);
        capture("sidebar-settings");
        QVERIFY(click("hopHomeTab")); QTRY_VERIFY(visual("hopLocationSearch"));
        capture("sidebar-connections");
        auto *dashboard = visual("hopDashboard"); QVERIFY(dashboard);
        const auto disconnectedColor = visual("hopConnectButton")->property("emphasisColor").value<QColor>();
        QVERIFY(dashboard->setProperty("activeLocationId", "Second"));
        QVERIFY(dashboard->setProperty("healthStatuses", QVariantMap{{"Second", QVariantMap{{"state", "reachable"}, {"latencyMs", 123}}}}));
        QVERIFY(dashboard->setProperty("connected", true));
        QTRY_VERIFY(visual("hopConnectionStatus")->property("text").toString().contains(QString::fromUtf8("Время ответа: 123 мс")));
        auto *firstHealthColumn = visual("hopHealthColumn_0"); QVERIFY(firstHealthColumn);
        QTRY_COMPARE(firstHealthColumn->width(), 112.0);
        const auto measuredColumnPosition = firstHealthColumn->mapToScene(QPointF()).x();
        QVERIFY(dashboard->setProperty("healthStatuses", QVariantMap{{"Second", QVariantMap{{"state", "unknown"}}}}));
        QTRY_VERIFY(visual("hopHealthStatus_0")->property("text").toString().contains(QString::fromUtf8("Не проверено")));
        QCOMPARE(firstHealthColumn->width(), 112.0);
        QCOMPARE(firstHealthColumn->mapToScene(QPointF()).x(), measuredColumnPosition);
        QVERIFY(dashboard->setProperty("healthStatuses", QVariantMap{{"Second", QVariantMap{{"state", "reachable"}, {"latencyMs", 123}}}}));
        QVERIFY(visual("hopConnectButton")->property("emphasisColor").value<QColor>() != disconnectedColor);
        QVERIFY(dashboard->setProperty("trafficRates", QVariantMap{{"vpnDown", 8.5}, {"vpnUp", 1.5}, {"directDown", 2.5}, {"directUp", 0.5}}));
        QVERIFY(visual("hopTrafficRates")->property("text").toString().contains(QString::fromUtf8("Мбит/с")));
        QVERIFY(visual("hopRoutingMode")->parentItem() == visual("hopProtocolLabel")->parentItem());
        QVERIFY(dashboard->setProperty("selectionStatus", QString::fromUtf8("Задержка выше порога; наблюдение перед переключением. Если превышение сохранится, подписка будет обновлена и начнётся выбор следующей локации.")));
        auto *selectionNotice = visual("hopSelectionStatus"); QVERIFY(selectionNotice);
        auto *connectionCard = visual("hopCurrentConnection"); QVERIFY(connectionCard);
        QTRY_VERIFY(selectionNotice->isVisible());
        QCOMPARE(selectionNotice->parentItem(), visual("hopProtocolLabel")->parentItem());
        QTRY_VERIFY(selectionNotice->mapToItem(connectionCard, QPointF(0, selectionNotice->height())).y() <= connectionCard->height());
        QCOMPARE(visual("hopConnectButton")->height(), 36.0);
        capture("sidebar-connected-latency");
        QVERIFY(dashboard->setProperty("selectionStatus", ""));
        QTRY_VERIFY(!selectionNotice->isVisible());
        QVERIFY(dashboard->setProperty("connected", false));
        QVERIFY(click("hopServiceActions_Second"));
        QTRY_VERIFY(visual("hopServiceRemove_Second") && visual("hopServiceRemove_Second")->isVisible());
        QVERIFY(click("hopServiceRemove_Second"));
        auto *remove = window->findChild<QObject *>("hopRemoveServiceDialog"); QVERIFY(remove);
        QTRY_VERIFY(remove->property("visible").toBool());
        QVERIFY(click("hopConfirmRemoveService"));
        QTRY_VERIFY(!core.m_serversRepository->orderedServerIds().contains("Second"));
        QVERIFY(core.m_serversRepository->orderedServerIds().contains("First"));
        QCOMPARE(warnings.count(), 0);
    }
    void apiDetailsStayInHopAndProtocolIsReadOnly()
    {
        SecureQSettings settings("Hop-Service-UI", "memory", nullptr, false, false);
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty("Debug", &Logger::Instance());
        engine.rootContext()->setContextProperty("IsMacOsNeBuild", false);
        engine.rootContext()->setContextProperty("IsPlayBuild", false);
        const auto vpn = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        TestCoreController core(vpn, &settings, &engine);
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        const auto api = amnezia::ApiV2ServerConfig::fromJson(QJsonObject {
            { "configVersion", serverConfigUtils::ConfigSource::AmneziaGateway }, { "description", "Synthetic provider" },
            { "api_config", QJsonObject { { "service_type", "external-premium" }, { "service_protocol", "awg" },
                { "active_device_count", 7 }, { "max_device_count", 7 },
                { "available_countries", QJsonArray { QJsonObject { { "server_country_code", "nl" }, { "server_country_name", "Netherlands" } },
                    QJsonObject { { "server_country_code", "de" }, { "server_country_name", "Germany" } } } } } },
            { "auth_data", QJsonObject { { "api_key", "synthetic" } } } });
        core.m_serversRepository->addServer("api", api.toJson(), serverConfigUtils::ConfigType::ExternalPremium);
        auto *health = core.findChild<hop::EndpointHealthController *>();
        auto *session = core.findChild<hop::LocationCoordinator *>();
        auto *catalog = core.findChild<hop::LocationCatalog *>();
        QVERIFY(health && session && catalog);
        QVERIFY(!session->checkCurrentLocation("api|nl"));
        session->locationSampled("api|nl", 42); session->locationSampled("api|de", 110);
        session->locationSampled("missing", 10);
        QCOMPARE(health->history("api|nl").value("average").toInt(), 42);
        QCOMPARE(health->history("api|de").value("average").toInt(), 110);
        QCOMPARE(health->history("missing").value("count").toInt(), 0);
        QCOMPARE(catalog->serviceInfo("api|nl").value("activeConfigurations").toInt(), 7);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Pages2"
ApplicationWindow {
    visible: true; width: 760; height: 680
    Item { objectName: "defaultFocusItem" }
    PageHomeHop { anchors.fill: parent }
})", QUrl("qrc:/hop-service-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(!window->findChild<QObject *>("hopProtocolButton"));
        auto *protocol = window->findChild<QObject *>("hopProtocolLabel"); QVERIFY(protocol);
        QVERIFY(!protocol->inherits("QQuickAbstractButton"));
        auto *page = window->findChild<QObject *>("hopHomePage"); QVERIFY(page);
        auto *pages = core.findChild<PageController *>(); QVERIFY(pages);
        QSignalSpy preparationErrors(pages, SIGNAL(showErrorMessage(QString)));
        const auto limitMessage = QString::fromUtf8("Лимит конфигураций подписки исчерпан, код 1108");
        emit session->preparationFailed(limitMessage);
        QTRY_COMPARE(preparationErrors.count(), 1);
        QCOMPARE(preparationErrors.first().first().toString(), limitMessage);
        auto *statistics = window->findChild<QObject *>("hopLocationStatsDialog"); QVERIFY(statistics);
        QVERIFY(!statistics->property("visible").toBool());
        // Отдельная проверка не открывает окно статистики и не выдаёт API-конфигурацию.
        auto *dashboard = findVisualItem(window->contentItem(), "hopDashboard");
        QVERIFY(dashboard);
        QVERIFY(QMetaObject::invokeMethod(dashboard, "locationHealthRequested", Q_ARG(QString, QString("api|nl"))));
        QVERIFY(!statistics->property("visible").toBool());
        QCOMPARE(health->history("api|nl").value("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(page, "openServerDetails", Q_ARG(QVariant, QVariant("api|nl"))));
        auto *dialog = window->findChild<QObject *>("hopServiceDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("info").toMap().value("kind").toString(), QStringLiteral("api"));
        QCOMPARE(dialog->property("info").toMap().value("maxConfigurations").toInt(), 7);
        QTest::qWait(150);
        const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath("native-hop-service.png"))); }
        QCOMPARE(warnings.count(), 0);
    }
    void routingEditorUsesSavedPolicy() {
        QTemporaryDir cache;
        SecureQSettings settings("Hop-Routing-UI-Test", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, cache.path(), {}, &settings);
        hop::TrafficPolicyController policy(&settings, &datasets);
        QTRY_VERIFY(policy.ready());
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty("RoutingPolicy", &policy);
        engine.rootContext()->setContextProperty("RouteDatasets", &datasets);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true; width: 520; height: 680
    HopRoutesEditor { anchors.fill: parent; anchors.margins: 16; policy: RoutingPolicy }
})", QUrl("qrc:/hop-routing-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *input = window->findChild<QObject *>("hopRoutesValue"); auto *add = window->findChild<QObject *>("hopRoutesAdd");
        QVERIFY(input && add); QVERIFY(input->setProperty("text", "example.com"));
        QTRY_VERIFY(add->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
        QTRY_VERIFY(!policy.busy()); QCOMPARE(policy.rules().size(), 1); QVERIFY(policy.enabled());
        QVERIFY(policy.addRule("ip", "192.0.2.0/24", "direct")); QTRY_VERIFY(!policy.busy());
        QCOMPARE(policy.rules().size(), 2);
        auto *dat = window->findChild<QObject *>("hopRoutesDat"); QVERIFY(dat);
        QVERIFY(QMetaObject::invokeMethod(dat, "clicked")); QCoreApplication::processEvents();
        auto *dialog = window->findChild<QObject *>("hopGeoDataDialog");
        if (!dialog) { dialog = window->contentItem()->findChild<QObject *>("hopGeoDataDialog"); }
        QVERIFY(dialog); QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QSignalSpy applied(&policy, &hop::TrafficPolicyController::policyChanged);
        const auto original = policy.rules();
        const auto down = [&]() -> QQuickItem * { return findVisualItem(window->contentItem(), QStringLiteral("hopRoutesDown_0")); };
        QTRY_VERIFY(down()); QVERIFY(QMetaObject::invokeMethod(down(), "clicked"));
        QVERIFY(policy.orderPending()); QCOMPARE(applied.count(), 0);
        auto *pending = window->findChild<QObject *>("hopRoutesOrderPending");
        auto *applyOrder = window->findChild<QObject *>("hopRoutesOrderApply");
        auto *discardOrder = window->findChild<QObject *>("hopRoutesOrderDiscard");
        QVERIFY(pending && applyOrder && discardOrder);
        QTRY_VERIFY(pending->property("visible").toBool()); QVERIFY(!dat->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(discardOrder, "clicked"));
        QVERIFY(!policy.orderPending()); QCOMPARE(policy.rules(), original); QCOMPARE(applied.count(), 0);
        QTRY_VERIFY(down()); QVERIFY(QMetaObject::invokeMethod(down(), "clicked"));
        QTest::qWait(150);
        const auto pendingCapture = qEnvironmentVariable("HOP_ROUTING_SCREENSHOT");
        if (!pendingCapture.isEmpty()) { QVERIFY(window->grabWindow().save(pendingCapture + ".pending.png")); }
        QVERIFY(QMetaObject::invokeMethod(applyOrder, "clicked")); QTRY_VERIFY(!policy.busy());
        QVERIFY(!policy.orderPending()); QCOMPARE(applied.count(), 1);
        QCOMPARE(policy.rules().first(), original.last());
        QTest::qWait(150);
        const auto screenshot = qEnvironmentVariable("HOP_ROUTING_SCREENSHOT");
        if (!screenshot.isEmpty()) { QVERIFY(window->grabWindow().save(screenshot)); }
        QCOMPARE(warnings.count(), 0);
    }
    void initTestCase()
    {
        QCoreApplication::setApplicationName(QStringLiteral("Hop"));
        const auto fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/pt-root-ui_vf.ttf"));
        QVERIFY(fontId >= 0);
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        QVERIFY(!families.isEmpty());
        QApplication::setFont(QFont(families.first(), 10));
        hop::applyDesktopStyle();
        PageLoader::declareQmlPageEnum();
    }

    void retainedProtectionHasExplicitReleaseAction()
    {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true
    width: 760
    height: 680
    HopDashboard {
        objectName: "protectionDashboard"
        anchors.fill: parent
        selectedServerId: "synthetic"
        selectedServerName: "Synthetic test location"
    }
})", QUrl(QStringLiteral("qrc:/hop-protection-test.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *root = engine.rootObjects().first();
        auto *dashboard = root->findChild<QObject *>(QStringLiteral("protectionDashboard"));
        auto *release = root->findChild<QObject *>(QStringLiteral("hopReleaseProtectionButton"));
        QVERIFY(dashboard);
        QVERIFY(release);
        QVERIFY(!release->property("visible").toBool());
        QVERIFY(dashboard->setProperty("protectionActive", true));
        QTRY_VERIFY(release->property("visible").toBool());
        QSignalSpy requests(dashboard, SIGNAL(releaseProtectionRequested()));
        QVERIFY(QMetaObject::invokeMethod(release, "clicked"));
        QCOMPARE(requests.count(), 1);
        QVERIFY(dashboard->setProperty("connected", true));
        QTRY_VERIFY(!release->property("visible").toBool());
        QVERIFY(dashboard->setProperty("connected", false));
        QVERIFY(dashboard->setProperty("selectedServerId", QString()));
        QVERIFY(dashboard->setProperty("connecting", true));
        auto *cancel = root->findChild<QObject *>(QStringLiteral("hopConnectButton"));
        QVERIFY(cancel && cancel->property("enabled").toBool());
        QSignalSpy cancellations(dashboard, SIGNAL(connectRequested()));
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QCOMPARE(cancellations.count(), 1);
        QCOMPARE(warnings.count(), 0);
    }

    void emptyStartupDispatchesHttpsFromDashboard()
    {
        SecureQSettings settings(QStringLiteral("Hop-ImportTests-") + QUuid::createUuid().toString(),
                                 QStringLiteral("isolated"), nullptr, false);
        const auto settingsCleanup = qScopeGuard([&settings]() { settings.clearSettings(); });
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty(QStringLiteral("Debug"), &Logger::Instance());
        engine.rootContext()->setContextProperty(QStringLiteral("IsMacOsNeBuild"), false);
        engine.rootContext()->setContextProperty(QStringLiteral("IsPlayBuild"), false);
        const auto vpn = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        TestCoreController core(vpn, &settings, &engine);
        const auto qmlCleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        QSignalSpy importErrors(core.findChild<ImportUiController *>(), &ImportUiController::importErrorOccurred);
        auto *subscriptions = core.findChild<hop::HttpSubscriptionsController *>();
        QVERIFY(subscriptions);
        QSignalSpy subscriptionErrors(subscriptions, &hop::HttpSubscriptionsController::errorOccurred);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Pages2"
ApplicationWindow {
    visible: true
    width: 760
    height: 680
    Item { objectName: "defaultFocusItem" }
    PageStart { anchors.fill: parent }
})", QUrl(QStringLiteral("qrc:/hop-initial-import-test.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root = engine.rootObjects().first();
        auto *settingsTab = root->findChild<QObject *>(QStringLiteral("hopSettingsTab"));
        QVERIFY(settingsTab);
        QVERIFY(QMetaObject::invokeMethod(settingsTab, "clicked"));
        QTRY_VERIFY(root->findChild<QObject *>(QStringLiteral("hopAboutButton")));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("hopAboutButton")), "clicked"));
        QTRY_VERIFY(root->findChild<QObject *>(QStringLiteral("hopAboutPage")));
        auto *homeTab = root->findChild<QObject *>(QStringLiteral("hopHomeTab"));
        QVERIFY(homeTab);
        QVERIFY(QMetaObject::invokeMethod(homeTab, "clicked"));
        QTRY_VERIFY(root->findChild<QObject *>(QStringLiteral("hopAddButton")));
        QTRY_VERIFY(root->findChild<QObject *>(QStringLiteral("hopAddButton"))
                    && root->findChild<QObject *>(QStringLiteral("hopAddButton"))->property("visible").toBool());
        auto *addButton = root->findChild<QObject *>(QStringLiteral("hopAddButton"));
        QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));
        auto *input = root->findChild<QObject *>(QStringLiteral("hopSubscriptionLink"));
        auto *button = root->findChild<QObject *>(QStringLiteral("hopSubscriptionAdd"));
        QVERIFY(input);
        QVERIFY(button);
        QVERIFY(input->setProperty("text", QStringLiteral("  https://provider.example/synthetic  ")));
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        // Запрос поставлен в очередь; цикл событий ещё не запущен, обращения к сети нет.
        QVERIFY(subscriptions->isBusy());
        QCOMPARE(importErrors.count(), 0);
        QCOMPARE(subscriptionErrors.count(), 0);
        QCOMPARE(core.m_serversRepository->serversCount(), 0);
        QCOMPARE(warnings.count(), 0);
    }

    void nativeModelAndBatchRefresh()
    {
        SecureQSettings settings(QStringLiteral("Hop-DashboardTests-") + QUuid::createUuid().toString(),
                                 QStringLiteral("isolated"), nullptr, false);
        const auto settingsCleanup = qScopeGuard([&settings]() { settings.clearSettings(); });
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty(QStringLiteral("Debug"), &Logger::Instance());
        const auto vpn = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        TestCoreController core(vpn, &settings, &engine);
        const auto qmlCleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        const auto createConfig = [&core](const QString &name) {
            const auto imported = core.m_importCoreController->extractConfigFromData(
                QStringLiteral("vless://00000000-0000-4000-8000-000000000001@vpn.example:443?security=none&type=tcp#") + name);
            auto config = imported.config;
            config.insert(QStringLiteral("hopSubscription"), QJsonObject {
                { QStringLiteral("sourceId"), QStringLiteral("synthetic-source") },
                { QStringLiteral("sourceUrl"), QStringLiteral("https://provider.example/synthetic") },
                { QStringLiteral("sourceName"), QStringLiteral("Synthetic") },
                { QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) }
            });
            return config;
        };
        QVERIFY(core.m_serversRepository->applyNativeSubscriptionSnapshot(QStringLiteral("synthetic-source"), {
            { QStringLiteral("first"), createConfig(QStringLiteral("First")) },
            { QStringLiteral("second"), createConfig(QStringLiteral("Second")) }
        }));
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Pages2"
ApplicationWindow {
    visible: true
    width: 760
    height: 680
    Item { objectName: "defaultFocusItem" }
    PageHome { anchors.fill: parent }
})", QUrl(QStringLiteral("qrc:/hop-native-test.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        QTRY_VERIFY(root->findChild<QObject *>(QStringLiteral("hopServersList")));
        auto *list = root->findChild<QObject *>(QStringLiteral("hopServersList"));
        QTRY_COMPARE(list->property("count").toInt(), 2);
        QTRY_VERIFY(findVisualItem(window->contentItem(), QStringLiteral("hopSelectServer_1")));
        QVERIFY(QMetaObject::invokeMethod(findVisualItem(window->contentItem(), QStringLiteral("hopSelectServer_1")), "clicked"));
        QCOMPARE(core.m_serversRepository->defaultServerId(), QStringLiteral("second"));
        QVERIFY(core.m_serversRepository->applyNativeSubscriptionSnapshot(QStringLiteral("synthetic-source"), {
            { QStringLiteral("second"), createConfig(QStringLiteral("Renamed")) }
        }));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QCOMPARE(core.m_serversUiController->property("defaultServerName").toString(), QStringLiteral("Renamed"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("hopAddButton")), "clicked"));
        QTRY_VERIFY(root->findChild<QObject *>(QStringLiteral("hopSubscriptionLink"))->property("visible").toBool());
        QTest::qWait(250);
        const auto dashboardImage = window->grabWindow();
        QVERIFY(!dashboardImage.isNull());
        const auto capturePath = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        if (!capturePath.isEmpty()) { QVERIFY(dashboardImage.save(QDir(capturePath).filePath(QStringLiteral("native-dashboard.png")))); }
        QCOMPARE(warnings.count(), 0);
    }

    void locationPolicyDialogEditsCountryPool()
    {
        SecureQSettings settings(QStringLiteral("Hop-Policy-Test"), QStringLiteral("memory"), nullptr, false, false);
        SecureServersRepository servers(&settings);
        SecureAppSettingsRepository appSettings(&settings);
        ImportController importer(&servers, &appSettings);
        for (const auto &label : { QStringLiteral("NL test"), QStringLiteral("DE test") }) {
            const auto result = importer.extractConfigFromData(QStringLiteral("vless://00000000-0000-4000-8000-000000000001@vpn.example:443?security=none&type=tcp#") + label);
            QVERIFY(result.errorCode == ErrorCode::NoError);
            importer.importConfig(result.config);
        }
        hop::LocationCatalog catalog(&servers, &settings);
        QVERIFY(catalog.setCountry(catalog.entries().at(0).id, QStringLiteral("NL")));
        QVERIFY(catalog.setCountry(catalog.entries().at(1).id, QStringLiteral("DE")));
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty(QStringLiteral("SyntheticCatalog"), &catalog);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true
    width: 520
    height: 680
    HopLocationsDialog { id: policy; catalog: SyntheticCatalog; anchors.centerIn: parent }
    Component.onCompleted: policy.open()
})", QUrl(QStringLiteral("qrc:/hop-policy-dialog-test.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        auto *country = root->findChild<QObject *>(QStringLiteral("hopPoolCountry"));
        auto *toggle = root->findChild<QObject *>(QStringLiteral("hopTogglePoolCountry"));
        auto *mode = root->findChild<QObject *>(QStringLiteral("hopSelectionMode"));
        auto *list = root->findChild<QObject *>(QStringLiteral("hopPriorityList"));
        QVERIFY(country && toggle && mode && list);
        QTRY_COMPARE(country->property("count").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QVERIFY(mode->setProperty("currentIndex", 1));
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 1)));
        QCOMPARE(catalog.mode(), int(hop::LocationCatalog::Automatic));
        QCOMPARE(catalog.candidateIds().size(), 1);
        QVERIFY(country->setProperty("currentIndex", 1));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QTRY_COMPARE(list->property("count").toInt(), 2);
        QCOMPARE(catalog.candidateIds().size(), 2);
        QCOMPARE(mode->property("count").toInt(), 4);
        for (const auto value : {hop::LocationCatalog::AllOrdered, hop::LocationCatalog::AllFastest}) {
            QVERIFY(mode->setProperty("currentIndex", int(value) - 1));
            QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, int(value) - 1)));
            QCOMPARE(catalog.mode(), int(value));
            QCOMPARE(catalog.candidateIds().size(), 2);
            QVERIFY(!list->property("visible").toBool());
            QVERIFY(!country->property("visible").toBool());
        }
        auto *popup = mode->property("popup").value<QObject *>(); QVERIFY(popup);
        auto *modeItem = qobject_cast<QQuickItem *>(mode); QVERIFY(modeItem);
        modeItem->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("visible").toBool());
        const auto capturePath = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        QTest::qWait(100);
        if (!capturePath.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capturePath).filePath(QStringLiteral("native-selection-menu.png")))); }
        QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(catalog.mode(), int(hop::LocationCatalog::Manual));
        auto *description = root->findChild<QObject *>("hopSelectionModeDescription"); QVERIFY(description);
        QTRY_VERIFY(description->property("text").toString().contains(QString::fromUtf8("повторяет")));
        modeItem->forceActiveFocus(); QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(catalog.mode(), int(hop::LocationCatalog::Automatic));
        QTRY_VERIFY(description->property("text").toString().contains(QString::fromUtf8("автопула")));
        QTest::qWait(100);
        if (!capturePath.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capturePath).filePath(QStringLiteral("native-location-policy.png")))); }
        QCOMPARE(warnings.count(), 0);
    }

    void routeDataDialogUsesNativeCatalog()
    {
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        hop::RouteDatasetsController datasets(nullptr, cache.path());
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/ui/qml/Modules/"));
        engine.rootContext()->setContextProperty(QStringLiteral("RouteDatasets"), &datasets);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true
    width: 760
    height: 700
    HopGeoDataDialog { id: catalog }
    Component.onCompleted: catalog.open()
})", QUrl(QStringLiteral("qrc:/hop-geodata-dialog-test.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto qmlCleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *root = engine.rootObjects().first();
        auto *list = root->findChild<QObject *>(QStringLiteral("hopGeoDataCategories"));
        QVERIFY(list);
        QTemporaryFile input;
        QVERIFY(input.open());
        input.write(QByteArray::fromHex("0a0e0a02525512080a04c00002001018"));
        input.close();
        QVERIFY(datasets.importFile(QUrl::fromLocalFile(input.fileName()), QStringLiteral("geoip")));
        QTRY_VERIFY(!datasets.isBusy());
        QVERIFY2(datasets.lastError().isEmpty(), qPrintable(datasets.lastError()));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        auto *window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        QTest::qWait(200);
        const auto catalogImage = window->grabWindow();
        QVERIFY(!catalogImage.isNull());
        const auto capturePath = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        if (!capturePath.isEmpty()) { QVERIFY(catalogImage.save(QDir(capturePath).filePath(QStringLiteral("native-dat-catalog.png")))); }
        QCOMPARE(warnings.count(), 0);
    }
};

QTEST_MAIN(TestHopDashboard)
#include "testHopDashboard.moc"
