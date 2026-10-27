#pragma once
#include <QTemporaryDir>
#include "core/routing/routeDatasetsController.h"
#include "awgFaultProbe.h"

inline int probeDatasetsInVm(QTextStream &output)
{
    const QJsonObject guard { { "vpnServer", "192.0.2.99" }, { "hopTransportPort", 9 },
        { "hopTransportProtocol", "udp" }, { "killSwitchOption", true }, { "splitTunnelType", 0 } };
    const auto protectedNow = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->applyTrafficGuard(guard, QString()); return reply.waitForFinished(5000) && reply.returnValue();
    }, [](){return false;});
    const auto cleanup = qScopeGuard([](){ IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->clearTrafficGuard(); reply.waitForFinished(5000);
    }); });
    if (!protectedNow || !awgPhysicalRequest(output, "DAT other process before download", "1.0.0.1", false)) { return 11; }
    QTemporaryDir cache;
    if (!cache.isValid()) { return 11; }
    SecureQSettings settings("Hop-DAT-VM", "memory", nullptr, true, false);
    QString helper = QCoreApplication::applicationDirPath() + "/Hop.exe";
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    const auto service = manager ? OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_STATUS) : nullptr;
    SERVICE_STATUS_PROCESS status {}; DWORD required = 0;
    const bool installedRunning = service && QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status), sizeof(status), &required) && status.dwCurrentState == SERVICE_RUNNING;
    if (service) { CloseServiceHandle(service); } if (manager) { CloseServiceHandle(manager); }
    if (installedRunning) {
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, status.dwProcessId);
        const auto close = qScopeGuard([&]() { if (process) { CloseHandle(process); } });
        wchar_t image[32768]; DWORD length = 32768;
        const auto root = QStringLiteral("C:/Program Files/Hop VM Install");
        if (!process || !QueryFullProcessImageNameW(process, 0, image, &length)
            || QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(image, length))).compare(
                root + "/Hop-service.exe", Qt::CaseInsensitive) != 0) { return 11; }
        QFile installed(root + "/Hop.exe"), staged(helper);
        if (!installed.open(QIODevice::ReadOnly) || !staged.open(QIODevice::ReadOnly)
            || QCryptographicHash::hash(installed.readAll(), QCryptographicHash::Sha256)
                != QCryptographicHash::hash(staged.readAll(), QCryptographicHash::Sha256)) { return 11; }
        // Узкие WFP-разрешения службы привязаны к её установленному клиенту, не к тестовой копии.
        helper = root + "/Hop.exe";
        output << "DAT helper identity: verified installed client" << Qt::endl;
    }
    hop::RouteDatasetsController datasets(nullptr, cache.path(), helper, &settings);
    for (const auto &kind : { QStringLiteral("geoip"), QStringLiteral("geosite") }) {
        QEventLoop wait; QTimer deadline; deadline.setSingleShot(true); deadline.setInterval(48000);
        QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
        QObject::connect(&datasets, &hop::RouteDatasetsController::changed, &wait, [&](){ if (!datasets.isBusy()) { wait.quit(); } });
        const QUrl url(QStringLiteral("https://github.com/kepler103b/geo-for-ru-only/releases/download/latest/") + kind + ".dat");
        if (!datasets.download(url, kind)) { return 11; }
        deadline.start(); if (datasets.isBusy()) { wait.exec(QEventLoop::ExcludeUserInputEvents); }
        output << "DAT " << kind << ": bytes=" << datasets.byteCount() << ", categories=" << datasets.categories().size()
            << ", valid=" << (!datasets.isBusy() && datasets.lastError().isEmpty()) << Qt::endl;
        if (datasets.isBusy() || !datasets.lastError().isEmpty() || datasets.kind() != kind || datasets.categories().isEmpty()) {
            output << "DAT failure: " << datasets.lastError() << Qt::endl; return 11;
        }
        // Проверяем полный публичный набор, а не только небольшой тестовый DAT.
        QJsonArray rules;
        qint64 entries = 0;
        const auto &data = datasets.data();
        for (const auto &category : data.categories) {
            rules.append(QJsonObject { { "kind", kind }, { "value", category.code }, { "target", "block" } });
            entries += category.entries;
        }
        const auto compiled = hop::compileTrafficPolicy(QJsonObject { { "default", "vpn" }, { "rules", rules } },
            kind == "geoip" ? &data : nullptr, kind == "geosite" ? &data : nullptr);
        const auto snapshot = hop::serializeTrafficPolicy(compiled);
        const auto restored = hop::deserializeTrafficPolicy(snapshot);
        const bool accepted = compiled.isValid() && restored.isValid() && restored.rules == compiled.rules
            && compiled.rules.size() == data.categories.size();
        output << "DAT " << kind << " complete category compilation/round trip: " << accepted
            << "; entries=" << entries << "; SHA256=" << datasets.sha256() << Qt::endl;
        if (!accepted) { return 11; }
        const auto digest = datasets.sha256();
        if (!datasets.loadCached(kind)) { return 11; }
        deadline.start(); if (datasets.isBusy()) { wait.exec(QEventLoop::ExcludeUserInputEvents); }
        const bool reopened = !datasets.isBusy() && datasets.lastError().isEmpty() && datasets.sha256() == digest;
        output << "DAT " << kind << " verified cache reopened: " << reopened << Qt::endl;
        if (!reopened) { return 11; }
    }
    if (!awgPhysicalRequest(output, "DAT other process after download", "1.0.0.1", false)) { return 11; }
    output << "DAT both sources stored with daily refresh: " << (datasets.sources().size() == 2) << Qt::endl;
    return datasets.sources().size() == 2 ? 0 : 11;
}
