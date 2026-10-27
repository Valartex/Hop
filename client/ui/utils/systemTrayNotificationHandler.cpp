/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <QDebug>
#include "systemTrayNotificationHandler.h"


#ifdef Q_OS_MAC
#  include "platforms/macos/macosstatusicon.h"
#endif

#include <QApplication>
#include <QDesktopServices>
#include <QIcon>
#include <QWindow>

#include "version.h"

SystemTrayNotificationHandler::SystemTrayNotificationHandler(QObject* parent) :
    NotificationHandler(parent)
#ifndef Q_OS_MAC
    , m_systemTrayIcon(parent)
#endif
{
    m_trayActionShow =  m_menu.addAction(QIcon(":/images/tray/application.png"), tr("Show") + " " + APPLICATION_NAME, this, [this](){
        emit raiseRequested();
    });
    m_menu.addSeparator();
    m_trayActionConnect = m_menu.addAction(tr("Connect"), this, [this](){ emit connectRequested(); });
    m_trayActionDisconnect = m_menu.addAction(tr("Disconnect"), this, [this](){ emit disconnectRequested(); });

    m_menu.addSeparator();

    m_trayActionVisitWebSite = m_menu.addAction(QIcon(":/images/tray/link.png"), tr("Visit Website"), [&](){
        QDesktopServices::openUrl(QUrl(websiteUrl));
    });

    if (QCoreApplication::applicationName() == QStringLiteral("Hop")) {
        m_menu.setObjectName(QStringLiteral("hopTrayMenu"));
        m_menu.setStyleSheet(QStringLiteral(
            "QMenu { background: #1a2028; color: #eef2f5; border: 1px solid #35424e; padding: 4px; }"
            "QMenu::item { color: #eef2f5; padding: 8px 24px; min-width: 140px; }"
            "QMenu::item:selected { background: #26333f; color: #eef2f5; }"
            "QMenu::item:disabled { color: #9aabb9; }"
            "QMenu::separator { height: 1px; background: #35424e; margin: 4px 8px; }"));
        m_trayActionShow->setIcon(QIcon(QStringLiteral(":/images/hop/hop.ico")));
        m_trayActionVisitWebSite->setVisible(false);
    }

    // Quit action: disconnect VPN first on macOS NE, else quit directly
    m_trayActionQuit = m_menu.addAction(QIcon(":/images/tray/cancel.png"),
                                       tr("Quit") + " " + APPLICATION_NAME,
                                       this,
                                       [&](){ qApp->quit(); });

#ifdef Q_OS_MAC
    // QSystemTrayIcon::setContextMenu crashes on macOS 14+: its menu-tracking
    // observer reads -[NSEvent clickCount] off a non-mouse event. Own the
    // NSStatusItem and attach the native NSMenu instead.
    m_statusIcon = new MacOSStatusIcon(this);
    m_statusIcon->setMenu(&m_menu);
#else
    m_systemTrayIcon.setToolTip(APPLICATION_NAME);
    connect(&m_systemTrayIcon, &QSystemTrayIcon::activated, this,
            &SystemTrayNotificationHandler::onTrayActivated);
    m_systemTrayIcon.setContextMenu(&m_menu);
#endif
    setTrayState(Vpn::ConnectionState::Disconnected);
#ifndef Q_OS_MAC
    m_systemTrayIcon.show();
#endif
}

SystemTrayNotificationHandler::~SystemTrayNotificationHandler() {
#ifdef Q_OS_MAC
    delete m_statusIcon;  // before m_menu: the status item references its NSMenu
    m_statusIcon = nullptr;
#endif
}

void SystemTrayNotificationHandler::setConnectionState(Vpn::ConnectionState state)
{
    m_connectionState = state;
    setTrayState(state);
    updateConnectionToolTip();
    NotificationHandler::setConnectionState(state);
}

void SystemTrayNotificationHandler::onTranslationsUpdated()
{
    m_trayActionShow->setText(tr("Show") + " " + APPLICATION_NAME);
    m_trayActionConnect->setText(tr("Connect"));
    m_trayActionDisconnect->setText(tr("Disconnect"));
    m_trayActionVisitWebSite->setText(tr("Visit Website"));
    m_trayActionQuit->setText(tr("Quit")+ " " + APPLICATION_NAME);
    updateConnectionToolTip();
}

void SystemTrayNotificationHandler::setConnectionDetails(const QString &subscription, const QString &location, int latencyMs)
{
    m_subscriptionName = subscription;
    m_locationName = location;
    m_latencyMs = latencyMs;
    updateConnectionToolTip();
}

void SystemTrayNotificationHandler::updateConnectionToolTip()
{
    m_connectionToolTip = QStringLiteral(APPLICATION_NAME);
    if (QCoreApplication::applicationName() == QStringLiteral("Hop")) {
        const bool connected = m_connectionState == Vpn::Connected;
        const bool active = connected || m_connectionState == Vpn::Preparing || m_connectionState == Vpn::Connecting
            || m_connectionState == Vpn::Reconnecting;
        m_connectionToolTip += QStringLiteral(" · ") + (connected ? tr("Подключено") : active ? tr("Подключение…") : tr("Отключено"));
        if (active) {
            // Windows ограничивает подсказку трея: оставляем место всем трём сведениям.
            const auto compact = [](QString text, int limit) {
                text.replace(QChar('\n'), QChar(' ')); text.replace(QChar('\r'), QChar(' '));
                return text.size() <= limit ? text : text.left(limit - 1) + QChar(0x2026);
            };
            m_connectionToolTip += QStringLiteral("\n") + tr("Подписка: %1").arg(compact(m_subscriptionName, 25));
            m_connectionToolTip += QStringLiteral("\n") + tr("Локация: %1").arg(compact(m_locationName, 32));
            m_connectionToolTip += QStringLiteral("\n") + (connected && m_latencyMs >= 0
                ? tr("Время ответа: %1 мс").arg(m_latencyMs) : tr("Время ответа: нет данных"));
        }
    }
#ifndef Q_OS_MAC
    m_systemTrayIcon.setToolTip(m_connectionToolTip);
#endif
}

void SystemTrayNotificationHandler::updateWebsiteUrl(const QString &newWebsiteUrl) {
    if (QCoreApplication::applicationName() == QStringLiteral("Hop")) { return; }
    qDebug() << "Updated website URL:" << newWebsiteUrl;
    websiteUrl = newWebsiteUrl;
}

void SystemTrayNotificationHandler::setTrayIcon(const QString &iconPath)
{
#ifdef Q_OS_MAC
    m_statusIcon->setIcon(iconPath);
#else
    QIcon trayIconMask = QCoreApplication::applicationName() == QStringLiteral("Hop")
        ? QIcon(iconPath) : QIcon(QPixmap(iconPath).scaled(128,128));
    trayIconMask.setIsMask(QCoreApplication::applicationName() != QStringLiteral("Hop"));
    m_systemTrayIcon.setIcon(trayIconMask);
#endif
}

void SystemTrayNotificationHandler::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
        emit raiseRequested();
    }
}

void SystemTrayNotificationHandler::setTrayState(Vpn::ConnectionState state)
{
    QString resourcesPath = ":/images/tray/%1";
    QString connectedIcon = ConnectedTrayIconName;
    QString disconnectedIcon = DisconnectedTrayIconName;
    QString errorIcon = ErrorTrayIconName;
#ifdef Q_OS_WIN
    if (QCoreApplication::applicationName() == QStringLiteral("Hop")) {
        resourcesPath = QStringLiteral(":/images/hop/%1");
        connectedIcon = QStringLiteral("hop.ico");
        disconnectedIcon = errorIcon = QStringLiteral("hop-disconnected.ico");
    }
#endif

    switch (state) {
    case Vpn::ConnectionState::Disconnected:
        setTrayIcon(QString(resourcesPath).arg(disconnectedIcon));
        m_trayActionConnect->setEnabled(true);
        m_trayActionDisconnect->setEnabled(false);
        break;
    case Vpn::ConnectionState::Preparing:
        setTrayIcon(QString(resourcesPath).arg(disconnectedIcon));
        m_trayActionConnect->setEnabled(false);
        m_trayActionDisconnect->setEnabled(true);
        break;
    case Vpn::ConnectionState::Connecting:
        setTrayIcon(QString(resourcesPath).arg(disconnectedIcon));
        m_trayActionConnect->setEnabled(false);
        m_trayActionDisconnect->setEnabled(true);
        break;
    case Vpn::ConnectionState::Connected:
        setTrayIcon(QString(resourcesPath).arg(connectedIcon));
        m_trayActionConnect->setEnabled(false);
        m_trayActionDisconnect->setEnabled(true);
        break;
    case Vpn::ConnectionState::Disconnecting:
        setTrayIcon(QString(resourcesPath).arg(disconnectedIcon));
        m_trayActionConnect->setEnabled(false);
        m_trayActionDisconnect->setEnabled(true);
        break;
    case Vpn::ConnectionState::Reconnecting:
        setTrayIcon(QString(resourcesPath).arg(disconnectedIcon));
        m_trayActionConnect->setEnabled(false);
        m_trayActionDisconnect->setEnabled(true);
        break;
    case Vpn::ConnectionState::Error:
        setTrayIcon(QString(resourcesPath).arg(errorIcon));
        m_trayActionConnect->setEnabled(true);
        m_trayActionDisconnect->setEnabled(false);
        break;
    case Vpn::ConnectionState::Unknown:
    default:
        m_trayActionConnect->setEnabled(false);
        m_trayActionDisconnect->setEnabled(true);
        setTrayIcon(QString(resourcesPath).arg(disconnectedIcon));
    }

    //#ifdef Q_OS_MAC
    //    // Get theme from current user (note, this app can be launched as root application and in this case this theme can be different from theme of real current user )
    //    bool darkTaskBar = MacOSFunctions::instance().isMenuBarUseDarkTheme();
    //    darkTaskBar = forceUseBrightIcons ? true : darkTaskBar;
    //    resourcesPath = ":/images_mac/tray_icon/%1";
    //    useIconName = useIconName.replace(".png", darkTaskBar ? "@2x.png" : " dark@2x.png");
    //#endif
}


void SystemTrayNotificationHandler::notify(NotificationHandler::Message type,
                                           const QString& title,
                                           const QString& message,
                                           int timerMsec) {
  Q_UNUSED(type);

#ifdef Q_OS_MAC
  Q_UNUSED(timerMsec);
  m_statusIcon->showMessage(title, message);
#else
  QIcon icon(QCoreApplication::applicationName() == QStringLiteral("Hop")
      ? QStringLiteral(":/images/hop/hop.ico") : ConnectedTrayIconName);
  m_systemTrayIcon.showMessage(title, message, icon, timerMsec);
#endif
}

void SystemTrayNotificationHandler::showHideWindow() {
//  QmlEngineHolder* engine = QmlEngineHolder::instance();
//  if (engine->window()->isVisible()) {
//    engine->hideWindow();
//#ifdef MVPN_MACOS
//    MacOSUtils::hideDockIcon();
//#endif
//  } else {
//    engine->showWindow();
//#ifdef MVPN_MACOS
//    MacOSUtils::showDockIcon();
//#endif
//  }
}

