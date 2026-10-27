#ifndef XRAYPROTOCOL_H
#define XRAYPROTOCOL_H

#include "QProcess"
#include <QtCore/qsharedpointer.h>
#include <QHostAddress>
#include <QList>
#include <QNetworkProxy>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QElapsedTimer>
#include <array>

#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"
#include "core/utils/ipcClient.h"
#include "vpnProtocol.h"

class XrayProtocol : public VpnProtocol
{
public:
    XrayProtocol(const QJsonObject &configuration, QObject *parent = nullptr);
    virtual ~XrayProtocol() override;

    ErrorCode start() override;
    void stop() override;
    void stopAfterFailure() override;
    bool isCleanupComplete() const override { return m_operationDepth == 0 && !m_sessionActive && !m_cleanupPending && !m_xrayStopPending && !m_tun2socksProcess && !(m_releaseRequested && m_guardOwned); }
    QNetworkProxy localProxy() const;
    bool updateTrafficProtection(bool enabled) override;

private:
    friend class TestXrayProtocolLifecycle;
    ErrorCode setupRouting();
    ErrorCode startTun2Socks();
    void cleanup(bool releaseProtection);
    void finishOperation();
    int m_operationDepth = 0;
    bool m_stopRequested = false;
    bool m_releaseRequested = false;
    QJsonObject trafficGuardConfig() const;
    void pollTraffic();
    bool configureTrafficMetrics();
    QNetworkAccessManager *m_metricsNetwork = nullptr;
    QPointer<QNetworkReply> m_metricsReply;
    QTimer m_metricsTimer;
    QElapsedTimer m_metricsClock;
    std::array<qint64, 4> m_previousCounters {};
    quint16 m_metricsPort = 0;
    bool m_metricsBaseline = false;
    bool m_guardOwned = false;
    bool m_preserveProtection = false;
    QMetaObject::Connection m_serviceConnection;

    QJsonObject m_xrayConfig;
    amnezia::RouteMode m_routeMode;
    QList<QHostAddress> m_dnsServers;
    QString m_remoteAddress;

    QString m_socksUser;
    QString m_socksPassword;
    int m_socksPort = 10808;

    QSharedPointer<IpcProcessInterfaceReplica> m_tun2socksProcess;
    bool m_sessionActive = false;
    bool m_cleanupPending = false;
    bool m_stopping = false;
    bool m_xrayStopPending = false;
    QTimer m_retryTimer;
    quint64 m_processGeneration = 0;
    int m_tun2socksRetryCount = 0;
    static constexpr int maxTun2SocksRetries = 5;
    static constexpr int tun2socksRetryDelayMs = 400;
};

#endif // XRAYPROTOCOL_H
