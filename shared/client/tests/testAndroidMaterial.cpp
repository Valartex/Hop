#include <QDir>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlFileSelector>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

namespace {
QQuickItem *findItem(QQuickItem *item, const QString &name) {
    if (item->objectName() == name) { return item; }
    for (auto *child : item->childItems()) {
        if (auto *found = findItem(child, name)) { return found; }
    }
    return nullptr;
}
}

class AndroidMaterialTest : public QObject {
    Q_OBJECT
private slots:
    void phoneLayout_data() {
        QTest::addColumn<int>("width"); QTest::addColumn<bool>("dark"); QTest::addColumn<bool>("empty");
        QTest::newRow("360-light") << 360 << false << false;
        QTest::newRow("390-light") << 390 << false << false;
        QTest::newRow("360-dark") << 360 << true << false;
        QTest::newRow("390-dark") << 390 << true << false;
        QTest::newRow("360-empty-light") << 360 << false << true;
        QTest::newRow("360-empty-dark") << 360 << true << true;
    }
    void phoneLayout() {
        QFETCH(int, width); QFETCH(bool, dark); QFETCH(bool, empty);
        QQmlApplicationEngine engine;
        QQmlFileSelector selector(&engine); selector.setExtraSelectors({"android"});
        engine.addImportPath("qrc:/ui/qml/Modules/");
        engine.rootContext()->setContextProperty("AndroidAppearance", QVariantMap{{"dark", dark}});
        engine.rootContext()->setContextProperty("EmptyDashboard", empty);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Style 1.0
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true; width: 390; height: 844; color: HopStyle.background
    palette: HopStyle.palette
    Material.theme: AndroidAppearance.dark ? Material.Dark : Material.Light
    Material.accent: HopStyle.accent
    ListModel { id: sample; ListElement { serverId:"a"; name:"Amsterdam"; serviceId:"svc"; serviceName:"Demo VPN"; serverDescription:"VLESS / Reality"; isServerFromGatewayApi:false; isSubscriptionExpired:false; isSubscriptionExpiringSoon:false } }
    HopDashboard {
        anchors.fill: parent
        selectedServerId: EmptyDashboard ? "" : "a"; selectedServerName: "Demo VPN · Amsterdam"; selectedProtocol: "VLESS / Reality"
        subscriptionCount: EmptyDashboard ? 0 : 1; healthAvailable: true; locationsAvailable: true
        locationMarks: ({a:{serviceId:"svc",serviceName:"Demo VPN",available:true,country:"NL"}})
        healthStatuses: ({a:{state:"reachable",latencyMs:225}})
        serverModel: EmptyDashboard ? null : sample
    }
    HopMobileNavigation {
        x: 16; y: 16
        labels: ["VPN", "Маршруты", "Локации", "Настройки"]
    }
})", QUrl("qrc:/android-material-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->setWidth(width); QTest::qWait(150);
        QCOMPARE(window->color(), QColor(dark ? "#0f1514" : "#f4fbf8"));
        const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath(QString("material-%1-%2%3.png").arg(width).arg(dark ? "dark" : "light").arg(empty ? "-empty" : "")))); }
        const QStringList names = empty ? QStringList{"hopEmptyAddButton", "hopEmptyRestoreButton", "hopMobileMenu"}
            : QStringList{"hopConnectButton", "hopAddButton", "hopMobileMenu", "hopServiceToggle_svc"};
        for (const auto &name : names) {
            auto *item = findItem(window->contentItem(), name); QVERIFY2(item, qPrintable(name));
            if (name == QStringLiteral("hopConnectButton") || name == QStringLiteral("hopEmptyAddButton")) {
                QVERIFY2(item->property("symbol").isValid(), "Android button selector was not applied");
                QVERIFY(item->property("emphasized").toBool());
                QVERIFY(!item->property("flat").toBool());
                const auto background = item->property("background").value<QQuickItem *>();
                QVERIFY(background);
                QCOMPARE(background->property("color").value<QColor>(), QColor(dark ? "#80d6b0" : "#006c4e"));
                const auto content = item->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
                const auto luminance = [](QColor color) {
                    const auto channel = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
                    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
                };
                const auto foreground = content->property("color").value<QColor>();
                QVERIFY(foreground.isValid());
                const auto a = luminance(foreground);
                const auto b = luminance(background->property("color").value<QColor>());
                QVERIFY2((qMax(a, b) + 0.05) / (qMin(a, b) + 0.05) >= 4.5, "Primary button text lacks contrast");
            }
            const auto bounds = item->mapRectToScene(item->boundingRect());
            QVERIFY2(bounds.left() >= 0 && bounds.right() <= width + 1,
                qPrintable(QString("%1: x=%2 width=%3").arg(name).arg(bounds.left()).arg(bounds.width())));
            QVERIFY2(bounds.height() >= 48, qPrintable(name));
            QVERIFY2(bounds.bottom() <= window->height(), qPrintable(name));
        }
        if (!empty) {
            auto *toggle = findItem(window->contentItem(), QStringLiteral("hopServiceToggle_svc"));
            QVERIFY(toggle);
            if (auto *row = findItem(window->contentItem(), QStringLiteral("hopLocationRow_0"))) { QVERIFY(!row->isVisible()); }
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, toggle->mapToScene(toggle->boundingRect().center()).toPoint());
            QTRY_VERIFY(findItem(window->contentItem(), QStringLiteral("hopLocationRow_0"))
                && findItem(window->contentItem(), QStringLiteral("hopLocationRow_0"))->isVisible());
        }
        auto *menuButton = findItem(window->contentItem(), QStringLiteral("hopMobileMenu"));
        QVERIFY(menuButton);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, menuButton->mapToScene(menuButton->boundingRect().center()).toPoint());
        for (int i = 0; i < 4; ++i) {
            QTRY_VERIFY(findItem(window->contentItem(), QStringLiteral("hopMobileTab_%1").arg(i)));
        }
        QCOMPARE(warnings.count(), 0);
    }
    void restoreDialog() {
        QQmlApplicationEngine engine;
        QQmlFileSelector selector(&engine); selector.setExtraSelectors({"android"});
        engine.addImportPath("qrc:/ui/qml/Modules/");
        engine.rootContext()->setContextProperty("AndroidAppearance", QVariantMap{{"dark", true}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import Style 1.0
import "qrc:/ui/qml/Components"
ApplicationWindow {
    visible: true; width: 360; height: 844; color: HopStyle.background
    HopDialog {
        objectName: "restoreDialog"; anchors.centerIn: parent
        width: parent.width - 32; modal: true
        title: "Восстановить настройки?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; text: "Текущие настройки Hop будут заменены данными резервной копии."; wrapMode: Text.WordWrap; color: HopStyle.text }
    }
})", QUrl("qrc:/android-dialog-test.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        const auto cleanup = qScopeGuard([&engine]() { qDeleteAll(engine.rootObjects()); });
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *dialog = window->findChild<QObject *>("restoreDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTest::qWait(250);
        auto *header = dialog->property("header").value<QQuickItem *>(); QVERIFY(header);
        auto *title = header->findChild<QQuickItem *>("hopDialogTitle"); QVERIFY(title);
        QVERIFY(title->property("contentWidth").toReal() <= title->width() + 1);
        QVERIFY(header->height() >= title->height() + 40);
        const auto capture = qEnvironmentVariable("HOP_TEST_CAPTURE_DIR");
        if (!capture.isEmpty()) { QVERIFY(window->grabWindow().save(QDir(capture).filePath("material-restore-dialog.png"))); }
        QSignalSpy accepted(dialog, SIGNAL(accepted()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(warnings.count(), 0);
    }
};

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Material");
    const auto fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/pt-root-ui_vf.ttf"));
    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    if (!families.isEmpty()) {
        QFont::insertSubstitution(QStringLiteral("Roboto"), families.first());
        application.setFont(QFont(families.first(), 10));
    }
    AndroidMaterialTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "testAndroidMaterial.moc"
