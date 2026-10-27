#include "xray.h"
#include "core/utils/networkUtilities.h"
#ifdef Q_OS_MAC
#include "router_mac.h"
#endif

#include <QDebug>
#include <QNetworkInterface>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <amnezia_xray.h>
#ifdef HOP_DYNAMIC_XRAY
#include <QLibrary>
#include <QDir>
namespace {
struct XrayApi {
    QLibrary library;
    decltype(&amnezia_xray_configure) configure = nullptr;
    decltype(&amnezia_xray_free) freeResult = nullptr;
    decltype(&amnezia_xray_setloghandler) setLogHandler = nullptr;
    decltype(&amnezia_xray_setsockcallback) setSockCallback = nullptr;
    decltype(&amnezia_xray_start) start = nullptr;
    decltype(&amnezia_xray_stop) stop = nullptr;
    using DirectOpen = quint64 (*)(const char *, const char *, quint16, quint8, void *);
    using DirectClose = void (*)(quint64, void *);
    using SetDirectCallbacks = void (*)(DirectOpen, DirectClose, void *);
    SetDirectCallbacks setDirectCallbacks = nullptr;
    bool ready = false;
    bool load() {
        if (ready) { return true; }
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("amnezia_xray.dll")));
        // Go runtime нельзя выгружать из работающего процесса.
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!library.load()) { return false; }
        configure = reinterpret_cast<decltype(configure)>(library.resolve("amnezia_xray_configure"));
        freeResult = reinterpret_cast<decltype(freeResult)>(library.resolve("amnezia_xray_free"));
        setLogHandler = reinterpret_cast<decltype(setLogHandler)>(library.resolve("amnezia_xray_setloghandler"));
        setSockCallback = reinterpret_cast<decltype(setSockCallback)>(library.resolve("amnezia_xray_setsockcallback"));
        start = reinterpret_cast<decltype(start)>(library.resolve("amnezia_xray_start"));
        stop = reinterpret_cast<decltype(stop)>(library.resolve("amnezia_xray_stop"));
        setDirectCallbacks = reinterpret_cast<SetDirectCallbacks>(library.resolve("hop_xray_setdirectcallbacks"));
        ready = configure && freeResult && setLogHandler && setSockCallback && start && stop;
        return ready;
    }
};
XrayApi &xrayApi() { static XrayApi api; return api; }
}
#define amnezia_xray_configure xrayApi().configure
#define amnezia_xray_free xrayApi().freeResult
#define amnezia_xray_setloghandler xrayApi().setLogHandler
#define amnezia_xray_setsockcallback xrayApi().setSockCallback
#define amnezia_xray_start xrayApi().start
#define amnezia_xray_stop xrayApi().stop
#endif
#include <qdebug.h>

#ifdef Q_OS_DARWIN
    #include <arpa/inet.h>
    #include <cerrno>
    #include <cstddef>
    #include <cstdint>
    #include <cstring>
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <sys/socket.h>
#endif
#ifdef Q_OS_WIN
    #include "windowsTrafficGuard.h"
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif
#ifdef Q_OS_LINUX
    #include <sys/socket.h>
    #include "xray_defs.h"
#endif

bool Xray::startXray(const QString &cfg)
{
    qDebug() << "Xray::startXray()";
#ifdef HOP_DYNAMIC_XRAY
    if (!xrayApi().load()) { qWarning() << "Xray library unavailable"; return false; }
#endif
    auto document = QJsonDocument::fromJson(cfg.toUtf8()).object();
#ifdef HOP_DYNAMIC_XRAY
    // Нативный профиль не задаёт путь записи журнала от имени привилегированной службы.
    document.insert(QStringLiteral("log"), QJsonObject { { QStringLiteral("loglevel"), QStringLiteral("none") } });
#endif
    auto gatewayAndIface = NetworkUtilities::getGatewayAndIface();
#ifdef Q_OS_WIN
    WindowsTrafficGuard::endDirectPolicy();
    const auto physicalEndpoint = QHostAddress(document.take(QStringLiteral("hopPhysicalEndpoint")).toString());
    if (!physicalEndpoint.isNull()) { gatewayAndIface = NetworkUtilities::getGatewayAndIfaceTo(physicalEndpoint); }
    const auto policyMeta = document.take(QStringLiteral("hopPolicy")).toObject();
    auto outbounds = document.value(QStringLiteral("outbounds")).toArray();
    if (!outbounds.isEmpty()) {
        const auto servers = outbounds.first().toObject().value(QStringLiteral("settings")).toObject().value(QStringLiteral("vnext")).toArray();
        if (!servers.isEmpty()) {
            const auto host = servers.first().toObject().value(QStringLiteral("address")).toString();
            const auto address = QHostAddress(NetworkUtilities::getIPAddress(host));
            gatewayAndIface = NetworkUtilities::getGatewayAndIfaceTo(address);
            if (!gatewayAndIface.second.isValid()) { qWarning() << "[xray] no uplink route to server"; return false; }
        }
    }
#endif
    const QString defaultGateway = gatewayAndIface.first;
    const QNetworkInterface defaultIface = gatewayAndIface.second;
#ifdef HOP_DYNAMIC_XRAY
    if (!policyMeta.isEmpty()) {
        QString physicalAddress;
        for (const auto &entry : defaultIface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback()) { physicalAddress = entry.ip().toString(); break; }
        }
        if (physicalAddress.isEmpty()) { return false; }
        for (int index = 0; index < outbounds.size(); ++index) {
            auto outbound = outbounds.at(index).toObject();
            if (outbound.value(QStringLiteral("tag")) == QStringLiteral("hop-direct")) {
                outbound.insert(QStringLiteral("sendThrough"), physicalAddress); outbounds[index] = outbound;
            }
        }
        document.insert(QStringLiteral("outbounds"), outbounds);
        if (policyMeta.value(QStringLiteral("needsDirect")).toBool()) {
            if (!xrayApi().setDirectCallbacks || !WindowsTrafficGuard::beginDirectPolicy(defaultIface.index(), physicalAddress)) { return false; }
            xrayApi().setDirectCallbacks([](const char *source, const char *destination, quint16 port, quint8 protocol, void *) -> quint64 {
                return WindowsTrafficGuard::openDirectSocket(QString::fromLatin1(source), QString::fromLatin1(destination), port, protocol);
            }, [](quint64 lease, void *) { WindowsTrafficGuard::closeDirectSocket(lease); }, nullptr);
        }
    }
    bool started = false;
    const auto releaseDirectOnFailure = qScopeGuard([&]() { if (!started) { WindowsTrafficGuard::endDirectPolicy(); } });
#endif

#ifdef Q_OS_LINUX
    m_defaultIfaceName = defaultIface.name().toUtf8();
#else
    m_defaultIfaceIdx = defaultIface.index();
#endif
    if (defaultIface.index() > 0) {
        qDebug() << "[xray] using uplink interface:" << defaultIface.name() << "(" << defaultIface.index() << ")";
    }

#ifdef Q_OS_MAC
    m_uplinkIfaceName = defaultIface.name();
    m_uplinkGateway = defaultGateway;
    if (!m_uplinkIfaceName.isEmpty()) {
        const bool installed = RouterMac::Instance().routeAddXray(m_uplinkIfaceName, m_uplinkGateway);
        if (!installed) {
            qWarning() << "[xray] failed to install xray routes on" << m_uplinkIfaceName;
        }
    }
#endif

    if (auto err = amnezia_xray_setsockcallback(ctxSockCallback, this); err != nullptr) {
        qDebug() << "[xray] sockopt failed: " << err;
        amnezia_xray_free(err);
        return false;
    }

    amnezia_xray_setloghandler(ctxLogHandler, this);

    QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
    if (auto err = amnezia_xray_configure(bytes.data()); err != nullptr) {
        qWarning() << "[xray] configuration rejected";
        amnezia_xray_free(err);
        return false;
    }

    if (auto err = amnezia_xray_start(); err != nullptr) {
        qDebug() << "[xray] failed to start: " << err;
        amnezia_xray_free(err);
        return false;
    }

#ifdef HOP_DYNAMIC_XRAY
    started = true;
#endif
    return true;
}

bool Xray::stopXray()
{
    qDebug() << "Xray::stopXray()";
#ifdef Q_OS_WIN
    WindowsTrafficGuard::endDirectPolicy();
#endif
#ifdef HOP_DYNAMIC_XRAY
    if (!xrayApi().ready) { return true; }
#endif
    bool success = true;
    if (auto err = amnezia_xray_stop(); err != nullptr) {
        qDebug() << "[xray] failed to stop: " << err;
        amnezia_xray_free(err);
        success = false;
    }

#ifdef Q_OS_MAC
    if (!m_uplinkIfaceName.isEmpty()) {
        RouterMac::Instance().routeDeleteXray(m_uplinkIfaceName, m_uplinkGateway);
    }
    m_uplinkIfaceName.clear();
    m_uplinkGateway.clear();
#endif

    return success;
}

void Xray::logHandler(char* str)
{
    QMetaObject::invokeMethod(qApp, [str = QString::fromUtf8(str)] {
        qDebug() << "[xray]" << str;
    }, Qt::QueuedConnection);
}

void Xray::sockCallback(uintptr_t fd)
{
#ifdef Q_OS_MAC
    if (m_defaultIfaceIdx > 0) {
        setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &m_defaultIfaceIdx, sizeof(m_defaultIfaceIdx));
        setsockopt(fd, IPPROTO_IPV6, IPV6_BOUND_IF, &m_defaultIfaceIdx, sizeof(m_defaultIfaceIdx));
    }
#endif
#ifdef Q_OS_WIN
    if (DWORD idx = m_defaultIfaceIdx; idx > 0) {
        setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<char *>(&idx), sizeof(idx));
        idx = htonl(idx); // IP_UNICAST_IF expects index in network byte order
        setsockopt(fd, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char *>(&idx), sizeof(idx));
    }
#endif
#ifdef Q_OS_LINUX
    if (!m_defaultIfaceName.isEmpty()) {
        setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, m_defaultIfaceName.data(), m_defaultIfaceName.size());
        setsockopt(fd, SOL_SOCKET, SO_MARK, &amnezia::xray::xrayTrafficMark, sizeof(amnezia::xray::xrayTrafficMark));
    }
#endif
}
