#pragma once
#include "vpnProtocol.h"
#include <QTimer>
#include <memory>

class WireguardProtocol;
class XrayProtocol;

// AWG остаётся штатным транспортом; домены и DAT обрабатывает общий вход Xray.
class RoutedAwgProtocol final : public VpnProtocol {
public:
    explicit RoutedAwgProtocol(const QJsonObject &configuration, QObject *parent = nullptr);
    ~RoutedAwgProtocol() override;
    ErrorCode start() override;
    void stop() override;
    void stopAfterFailure() override;
    bool isCleanupComplete() const override;
    bool updateTrafficProtection(bool enabled) override;
private:
    friend class TestXrayProtocolLifecycle;
    RoutedAwgProtocol(const QJsonObject &configuration, std::unique_ptr<WireguardProtocol> transport, QObject *parent);
    void startFrontend();
    void fail(ErrorCode error);
    void cleanup(bool release);
    void finishCleanup();
    std::unique_ptr<WireguardProtocol> m_transport;
    std::unique_ptr<XrayProtocol> m_frontend;
    QTimer m_cleanupTimer;
    bool m_started = false, m_stopping = false, m_clean = true, m_release = false;
    bool m_cleanupInProgress = false;
    unsigned m_cleanupAttempts = 0;
};
