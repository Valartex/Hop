#ifndef WIREGUARDPROTOCOL_H
#define WIREGUARDPROTOCOL_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>
#include <QTimer>

#include "vpnProtocol.h"

#include "mozilla/controllerimpl.h"

class WireguardProtocol : public VpnProtocol
{
    Q_OBJECT

public:
    explicit WireguardProtocol(const QJsonObject& configuration, QObject* parent = nullptr);
    virtual ~WireguardProtocol() override;

    ErrorCode start() override;
    void stop() override;
    void stopAfterFailure() override;
    bool isCleanupComplete() const override;
    bool updateTrafficProtection(bool enabled) override;

    ErrorCode startMzImpl();
    ErrorCode stopMzImpl();

private:
    friend class TestXrayProtocolLifecycle;
    WireguardProtocol(const QJsonObject &configuration, ControllerImpl *controller, QObject *parent);
    bool applyTrafficProtection(const QString &tunnelAlias);
    void releaseTrafficProtection();
    QJsonObject m_guardConfig;
    bool m_guardOwned = false;
    bool m_releaseGuard = false;
    bool m_preserveProtection = false;
    bool m_started = false;
    bool m_starting = false;
    bool m_stopRequested = false;
    bool m_deactivationRequested = false;
    bool m_backendReady = false;

    QScopedPointer<ControllerImpl> m_impl;
};

#endif // WIREGUARDPROTOCOL_H
