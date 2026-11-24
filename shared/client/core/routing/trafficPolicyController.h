#pragma once
#include "trafficPolicy.h"
#include "routeDatasetsController.h"
#include <QFutureWatcher>

namespace hop {
class TrafficPolicyController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString defaultTarget READ defaultTarget NOTIFY defaultTargetChanged)
    Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
    Q_PROPERTY(bool orderPending READ orderPending NOTIFY changed)
public:
    TrafficPolicyController(SecureQSettings *settings, RouteDatasetsController *datasets, QObject *parent = nullptr,
                            const QJsonObject &legacyPolicy = {});
    ~TrafficPolicyController() override;
    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    bool ready() const { return m_ready; }
    QString lastError() const { return m_error; }
    QString defaultTarget() const { return m_policy.value(QStringLiteral("default")).toString(QStringLiteral("vpn")); }
    QVariantList rules() const;
    bool orderPending() const { return m_orderPending; }
    QJsonObject snapshot() const;
    Q_INVOKABLE bool addRule(const QString &kind, const QString &value, const QString &target);
    Q_INVOKABLE bool removeRule(int row);
    Q_INVOKABLE bool moveRule(int row, int delta);
    Q_INVOKABLE bool applyOrder();
    Q_INVOKABLE void discardOrder();
    Q_INVOKABLE bool setDefaultTarget(const QString &target);
    Q_INVOKABLE bool setEnabled(bool enabled);
    Q_INVOKABLE bool setRuleAutoUpdate(int row, bool enabled);
    Q_INVOKABLE bool refreshRule(int row);
    Q_INVOKABLE QString defaultSource(const QString &kind) const { return m_datasets->defaultSource(kind); }
    bool apply(const QJsonObject &policy, bool enabled);
signals:
    void changed();
    void rulesChanged();
    void defaultTargetChanged();
    void policyChanged();
private:
    void compile(const QJsonObject &policy, bool enabled, bool persist);
    bool saveSourcePolicy(const QJsonObject &policy, bool enabled);
    SecureQSettings *m_settings;
    RouteDatasetsController *m_datasets;
    QJsonObject m_policy, m_pending;
    QJsonArray m_orderDraft;
    CompiledTrafficPolicy m_compiled;
    QFutureWatcher<CompiledTrafficPolicy> m_watcher;
    bool m_enabled = false, m_busy = false, m_ready = false, m_pendingEnabled = false, m_persist = false, m_refreshPending = false;
    bool m_orderPending = false, m_applyingOrder = false;
    QString m_error;
    QVariantList m_notifiedRules;
    QString m_notifiedDefaultTarget;
};
}
