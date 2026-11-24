#include "amneziaApplication.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickStyle>
#include <QIcon>
#include <QPalette>
#include <QResource>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QTranslator>
#include <QEvent>
#include <QDir>
#include <QSettings>
#include <QJsonDocument>
#include <QSaveFile>
#include <QImage>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
#include <QtQuick/QQuickWindow>  
#include <QWindow>     

#include "core/controllers/updateController.h"
#include "core/protocols/qmlRegisterProtocols.h"
#include "logger.h"
#include "ui/controllers/qml/pageController.h"
#include "ui/models/installedAppsModel.h"
#include "ui/utils/mtProxyPublicHostInput.h"
#include "version.h"
#ifdef Q_OS_WIN
#include "hopUiVpnTest.h"
#include "hopDesktopStyle.h"
#endif
#include "core/utils/appUiConfig.h"

#include "platforms/ios/QRCodeReaderBase.h"
#ifdef Q_OS_IOS
    #include "platforms/ios/ioscontextmenu.h"
#endif

#ifdef Q_OS_ANDROID
#include "platforms/android/native/android_controller.h"
#include "platforms/android/native/appearance.h"
#endif
         

bool AmneziaApplication::m_forceQuit = false;

AmneziaApplication::AmneziaApplication(int &argc, char *argv[]) : AMNEZIA_BASE_CLASS(argc, argv),
      m_optAutostart({QStringLiteral("a"), QStringLiteral("autostart")}, QStringLiteral("System autostart")),
      m_optCleanup  ({QStringLiteral("c"), QStringLiteral("cleanup")}, QStringLiteral("Cleanup logs")),
      m_optConnect  ({QStringLiteral("connect")}, QStringLiteral("Connect to server by index on startup"), QStringLiteral("index")),
      m_optImport   ({QStringLiteral("import")}, QStringLiteral("Import configuration from data string"), QStringLiteral("data"))
{
    setDesktopFileName(QStringLiteral(APPLICATION_NAME));
    setQuitOnLastWindowClosed(false);

    // Fix config file permissions
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    {
        QSettings s(ORGANIZATION_NAME, APPLICATION_NAME);
        s.setValue("permFixed", true);
    }

    QString configLoc1 = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation).first() + "/" + ORGANIZATION_NAME + "/"
            + APPLICATION_NAME + ".conf";
    QFile::setPermissions(configLoc1, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QString configLoc2 = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation).first() + "/" + ORGANIZATION_NAME + "/"
            + APPLICATION_NAME + "/" + APPLICATION_NAME + ".conf";
    QFile::setPermissions(configLoc2, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif

#ifdef Q_OS_WIN
    // argv[0] может быть относительным; флаг уже проверен main до создания приложения.
    m_hopVpnTest = QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && arguments().size() == 3
        && (arguments().at(1) == QStringLiteral("--hop-ui-vpn-test") || arguments().at(1) == QStringLiteral("--hop-ui-api-vpn-test"));
    m_hopSmokeTest = m_hopVpnTest || (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && arguments().size() == 2
        && arguments().at(1) == QStringLiteral("--hop-ui-smoke-test"));
#endif
    const bool persistentApiTest = m_hopVpnTest && arguments().at(1) == QStringLiteral("--hop-ui-api-vpn-test");
    if (m_hopSmokeTest && !persistentApiTest) {
        // Обычный VM-стенд не перезаписывает пользовательский DAT-кэш.
        QStandardPaths::setTestModeEnabled(true);
    }
    m_settings = m_hopSmokeTest && !persistentApiTest ? new SecureQSettings("Hop-Smoke", "memory", this, true, false)
                              : new SecureQSettings(ORGANIZATION_NAME, APPLICATION_NAME, this);
    m_nam = new QNetworkAccessManager(this);
}

AmneziaApplication::~AmneziaApplication()
{
#ifdef AMNEZIA_DESKTOP
    if (m_vpnConnection && m_vpnConnectionThread.isRunning()) {
        QMetaObject::invokeMethod(m_vpnConnection.get(), "disconnectSlots", Qt::BlockingQueuedConnection);
        
        if (!m_hopSmokeTest || m_hopVpnTest) { QMetaObject::invokeMethod(m_vpnConnection.get(), "disconnectFromVpn", Qt::BlockingQueuedConnection); }
    }
#endif

    m_vpnConnectionThread.requestInterruption();
    m_vpnConnectionThread.quit();

    if (!m_vpnConnectionThread.wait(3000)) {
        m_vpnConnectionThread.terminate();
        m_vpnConnectionThread.wait(500);
    }

    if (m_engine) {
        delete m_engine;
    }
}

#ifdef Q_OS_ANDROID
namespace {
    static void clearQtCaches()
    {
        const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheRoot.isEmpty()) {
            QDir(cacheRoot + "/QtShaderCache").removeRecursively();
            QDir(cacheRoot + "/qmlcache").removeRecursively();
        }
    }
}
#endif

void AmneziaApplication::init()
{
    m_engine = new QQmlApplicationEngine;
#ifdef Q_OS_ANDROID
    m_engine->rootContext()->setContextProperty("AndroidAppearance", new hop::AndroidAppearance(m_engine));
#endif
    if (m_hopSmokeTest) {
        connect(m_engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &warnings) { m_hopSmokeWarnings += warnings.size(); });
    }

    const QUrl url(QStringLiteral(APP_QML_ENTRYPOINT));
    QObject::connect(
        m_engine, &QQmlApplicationEngine::objectCreated, this,
        [this, url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
                return;
            }
            // install filter on main window
            if (auto win = qobject_cast<QQuickWindow*>(obj)) {
                if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
                    win->setIcon(QIcon(QStringLiteral(":/images/hop/hop.ico")));
                }
                win->installEventFilter(this);
#ifdef Q_OS_ANDROID
                QObject::connect(win, &QQuickWindow::sceneGraphError,
                    [](QQuickWindow::SceneGraphError, const QString &msg) {
                        qWarning() << "Scene graph error (suppressed):" << msg;
                    });
                // Keep graphics context alive across hide/show cycles to avoid
                // eglSwapBuffers/makeCurrent being called on a context Android has reclaimed.
                win->setPersistentSceneGraph(true);
                win->setPersistentGraphics(true);
#endif
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
                win->show();
#else
                if (!m_coreController || !m_coreController->pageController()->shouldStartMinimized()) {
                    win->show();
                }
#endif
            }
        },
        Qt::QueuedConnection);

    m_engine->rootContext()->setContextProperty("Debug", &Logger::Instance());

#ifdef MACOS_NE
    m_engine->rootContext()->setContextProperty("IsMacOsNeBuild", true);
#else
    m_engine->rootContext()->setContextProperty("IsMacOsNeBuild", false);
#endif

#ifdef Q_OS_IOS
    m_engine->rootContext()->setContextProperty("IosContextMenu", new IosContextMenu(this));
#endif

#ifdef Q_OS_ANDROID
    m_engine->rootContext()->setContextProperty("IsPlayBuild", AndroidController::instance()->isPlay());
#else
    m_engine->rootContext()->setContextProperty("IsPlayBuild", false);
#endif

    m_vpnConnection.reset(new VpnConnection(nullptr, nullptr));
    m_vpnConnection->moveToThread(&m_vpnConnectionThread);
    m_vpnConnectionThread.start();

    m_coreController.reset(new CoreController(m_vpnConnection, m_settings, m_engine));

    m_engine->addImportPath(QStringLiteral(APP_QML_IMPORT_PATH));

    if (m_parser.isSet(m_optImport)) {
        const QString data = m_parser.value(m_optImport);
        if (!data.isEmpty()) {
            if (m_coreController) {
                m_coreController->importConfigFromData(data);
            }
        }
    }

    if (m_hopSmokeTest && !m_hopVpnTest) {
        m_coreController->importConfigFromData(QStringLiteral("vless://00000000-0000-4000-8000-000000000001@192.0.2.1:443?security=none&type=tcp#Hop%20UI%20Test"));
    }
    m_engine->load(url);

    m_coreController->setQmlRoot();
#ifdef Q_OS_WIN
    if (m_hopVpnTest) {
        QTimer::singleShot(500, this, [this]() {
            const int code = hop::runUiVpnTest(m_coreController.get(), m_engine, arguments().at(2), m_hopSmokeWarnings);
            exit(code);
        });
    } else if (m_hopSmokeTest) {
        QTimer::singleShot(2000, this, [this]() {
            m_coreController->pageController()->goToPage(PageLoader::PageEnum::PageSettingsSplitTunneling, false);
        });
        QTimer::singleShot(3000, this, [this]() {
            FILETIME created {}, ended {}, kernel {}, user {};
            if (GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
                m_hopSmokeCpuStart = (quint64(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime
                    + (quint64(user.dwHighDateTime) << 32) + user.dwLowDateTime;
                m_hopSmokeIdleClock.start();
            }
        });
        QTimer::singleShot(6000, this, [this]() {
            HANDLE token = nullptr; TOKEN_ELEVATION elevation {}; DWORD size = 0;
            bool ordinary = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)
                && GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && !elevation.TokenIsElevated;
            if (token) { CloseHandle(token); }
            auto *window = m_engine->rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
            const bool routes = window && window->findChild<QObject *>(QStringLiteral("hopRoutesEnabled"));
            auto *connection = m_coreController->findChild<ConnectionController *>();
            const bool service = connection && connection->isServiceReady();
            FILETIME created {}, ended {}, kernel {}, user {}; double idleCpu = -1;
            if (m_hopSmokeIdleClock.isValid() && m_hopSmokeIdleClock.elapsed() > 0 && GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
                const auto ticks = (quint64(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime
                    + (quint64(user.dwHighDateTime) << 32) + user.dwLowDateTime;
                idleCpu = double(ticks - m_hopSmokeCpuStart) / 100.0 / m_hopSmokeIdleClock.elapsed();
            }
            PROCESS_MEMORY_COUNTERS_EX memory {}; memory.cb = sizeof(memory);
            const bool measured = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
            const auto resultPath = QDir::tempPath() + QStringLiteral("/hop-ui-smoke-%1").arg(applicationPid());
            const bool image = window && window->grabWindow().save(resultPath + QStringLiteral(".png"));
            QSaveFile result(resultPath + QStringLiteral(".json"));
            const auto data = QJsonDocument(QJsonObject { { "ordinaryUser", ordinary }, { "routesVisible", routes },
                { "serviceReady", service }, { "qmlWarnings", m_hopSmokeWarnings }, { "imageSaved", image },
                { "idleCpuPercentOneCore", idleCpu }, { "workingSetMiB", measured ? double(memory.WorkingSetSize) / 1048576 : -1 },
                { "privateMiB", measured ? double(memory.PrivateUsage) / 1048576 : -1 } }).toJson(QJsonDocument::Compact);
            const bool saved = result.open(QIODevice::WriteOnly) && result.write(data) == data.size() && result.commit();
            exit(ordinary && routes && service && m_hopSmokeWarnings == 0 && image && saved ? 0 : 3);
        });
    }
#endif

#if CLIENT_ENABLE_APP_UPDATES
    m_coreController->checkForAppUpdates();
#endif

#ifdef Q_OS_WIN //TODO
    if (m_parser.isSet(m_optAutostart))
        m_coreController->pageController()->showOnStartup();
    else
        emit m_coreController->pageController()->raiseMainWindow();
#else
    m_coreController->pageController()->showOnStartup();
#endif

// Android TextArea clipboard workaround
// Text from TextArea always has "text/html" mime-type:
// /qt/6.6.1/Src/qtdeclarative/src/quick/items/qquicktextcontrol.cpp:1865
// Next, html is created for this mime-type:
// /qt/6.6.1/Src/qtdeclarative/src/quick/items/qquicktextcontrol.cpp:1885
// And this html goes to the Androids clipboard, i.e. text from TextArea is always copied as richText:
// /qt/6.6.1/Src/qtbase/src/plugins/platforms/android/androidjniclipboard.cpp:46
// So we catch all the copies to the clipboard and clear them from "text/html"
#ifdef Q_OS_ANDROID
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, []() {
        auto clipboard = QGuiApplication::clipboard();
        if (clipboard->mimeData()->hasHtml()) {
            clipboard->setText(clipboard->text());
        }
    });
#endif

    if (m_parser.isSet(m_optConnect)) {
        bool ok = false;
        int idx = m_parser.value(m_optConnect).toInt(&ok);
        if (ok) {
            QTimer::singleShot(0, this, [this, idx]() {
                if (m_coreController) {
                    m_coreController->openConnectionByIndex(idx);
                }
            });
        }
    }
}

void AmneziaApplication::registerTypes()
{
    qRegisterMetaType<ServerCredentials>("ServerCredentials");

    qRegisterMetaType<DockerContainer>("DockerContainer");
    using namespace amnezia::ProtocolEnumNS;
    qRegisterMetaType<TransportProto>("TransportProto");
    qRegisterMetaType<Proto>("Proto");
    qRegisterMetaType<ServiceType>("ServiceType");

    qmlRegisterType<QRCodeReader>("QRCodeReader", 1, 0, "QRCodeReader");

    m_containerProps.reset(new ContainerProps());
    qmlRegisterSingletonInstance("ContainerProps", 1, 0, "ContainerProps", m_containerProps.get());

    m_protocolProps.reset(new ProtocolProps());
    qmlRegisterSingletonInstance("ProtocolProps", 1, 0, "ProtocolProps", m_protocolProps.get());

    qmlRegisterSingletonType(QUrl("qrc:/ui/qml/Filters/ContainersModelFilters.qml"), "ContainersModelFilters", 1, 0,
                             "ContainersModelFilters");

    qmlRegisterType<InstalledAppsModel>("InstalledAppsModel", 1, 0, "InstalledAppsModel");

    qmlRegisterType<PublicHostInputValidator>("MtProxyConfig", 1, 0, "PublicHostInputValidator");
    qmlRegisterType<PublicHostInputValidator>("TelemtConfig", 1, 0, "PublicHostInputValidator");

    amnezia::declareQmlProtocolEnum();
    Vpn::declareQmlVpnConnectionStateEnum();
    PageLoader::declareQmlPageEnum();
    UpdateState::declareQmlUpdateStateEnum();
}

void AmneziaApplication::loadFonts()
{
#ifdef Q_OS_ANDROID
    if (applicationName() == QStringLiteral("Hop")) {
        QQuickStyle::setStyle("Material");
        auto systemFont = font();
        systemFont.setFamily(QStringLiteral("Roboto"));
        setFont(systemFont);
    } else
#endif
#ifdef Q_OS_WIN
    if (applicationName() == QStringLiteral("Hop")) {
        hop::applyDesktopStyle();
    } else
#endif
    {
        QQuickStyle::setStyle("Basic");
    }

    QFontDatabase::addApplicationFont(QStringLiteral(APP_UI_FONT_RESOURCE));
}

bool AmneziaApplication::parseCommands()
{
    m_parser.setApplicationDescription(APPLICATION_NAME);
    m_parser.addHelpOption();
    m_parser.addVersionOption();

    m_parser.addOption(m_optAutostart);
    m_parser.addOption(m_optCleanup);
    m_parser.addOption(m_optConnect);
    m_parser.addOption(m_optImport);
#ifdef Q_OS_WIN
    QCommandLineOption smoke(QStringLiteral("hop-ui-smoke-test"));
    smoke.setFlags(QCommandLineOption::HiddenFromHelp); m_parser.addOption(smoke);
    QCommandLineOption vpnTest(QStringLiteral("hop-ui-vpn-test"), QString(), QStringLiteral("pipe"));
    vpnTest.setFlags(QCommandLineOption::HiddenFromHelp); m_parser.addOption(vpnTest);
    QCommandLineOption apiVpnTest(QStringLiteral("hop-ui-api-vpn-test"), QString(), QStringLiteral("pipe"));
    apiVpnTest.setFlags(QCommandLineOption::HiddenFromHelp); m_parser.addOption(apiVpnTest);
#endif
    
    m_parser.process(*this);

    if (m_parser.isSet(m_optCleanup)) {
        Logger::cleanUp();
        QTimer::singleShot(100, this, [this] { quit(); });
        exec();
        return false;
    }
    return true;
}

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
void AmneziaApplication::startLocalServer() {
    const QString serverName(APP_INSTANCE_NAME);
    QLocalServer::removeServer(serverName);

    QLocalServer *server = new QLocalServer(this);
    server->listen(serverName);

    QObject::connect(server, &QLocalServer::newConnection, this, [server, this]() {
        if (server) {
            QLocalSocket *clientConnection = server->nextPendingConnection();
            clientConnection->deleteLater();
        }
        emit m_coreController->pageController()->raiseMainWindow(); //TODO
    });
}
#endif

bool AmneziaApplication::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Close) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        quit();
#else
        if (m_forceQuit) {
            quit();
        } else {
            if (m_coreController && m_coreController->pageController()) {
                m_coreController->pageController()->hideMainWindow();
            }
        }
#endif
        return true; // eat the close
    }
    // call base QObject::eventFilter
    return QObject::eventFilter(watched, event);
}

void AmneziaApplication::forceQuit()
{
    m_forceQuit = true;
    quit();
}

QQmlApplicationEngine *AmneziaApplication::qmlEngine() const
{
    return m_engine;
}

QNetworkAccessManager *AmneziaApplication::networkManager()
{
    return m_nam;
}

QClipboard *AmneziaApplication::getClipboard()
{
    return this->clipboard();
}
