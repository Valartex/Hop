#pragma once
#include <msi.h>
#include <QVersionNumber>
#include <functional>
#pragma comment(lib, "msi.lib")

// Этот сценарий вызывается только после проверки VirtualBox и владельца службы.
inline QStringList installedHopProducts()
{
    QStringList products;
    for (DWORD index = 0; index < 16; ++index) {
        wchar_t product[39] {};
        const auto result = MsiEnumRelatedProductsW(L"{FAE4DDF6-DA49-4DAE-9A6C-1D591CB5F57F}", 0, index, product);
        if (result == ERROR_NO_MORE_ITEMS) { return products; }
        if (result != ERROR_SUCCESS) { return {}; }
        products.append(QString::fromWCharArray(product));
    }
    return {};
}

inline QString installedHopVersion(const QString &product)
{
    wchar_t version[64] {}; DWORD length = 64;
    return MsiGetProductInfoW(reinterpret_cast<LPCWSTR>(product.utf16()), INSTALLPROPERTY_VERSIONSTRING,
        version, &length) == ERROR_SUCCESS ? QString::fromWCharArray(version) : QString();
}

inline int probeInstallerUpgradeInVm(const QString &phase, const QString &destination,
    const std::function<bool(bool)> &serviceMatches, QTextStream &output)
{
    const auto directory = QCoreApplication::applicationDirPath();
    const bool rollback = phase == QStringLiteral("rollback"), downgrade = phase == QStringLiteral("downgrade");
    const auto package = directory + (rollback ? "/Hop-Rollback-Test.msi" : downgrade ? "/Hop-Previous.msi" : "/Hop-Setup.msi");
    const auto before = installedHopProducts();
    if (before.size() != 1 || !serviceMatches(true) || !QFileInfo::exists(package)) { return 7; }
    const auto oldVersion = installedHopVersion(before.first());
    const auto fileHashes = [&]() {
        QList<QByteArray> hashes;
        for (const auto &name : { "Hop.exe", "Hop-service.exe", "agw.dll", "BUILD-MANIFEST.json" }) {
            QFile file(destination + QLatin1Char('/') + QLatin1String(name));
            if (!file.open(QIODevice::ReadOnly)) { return QList<QByteArray>(); }
            hashes.append(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
        }
        return hashes;
    };
    const auto oldHashes = fileHashes();
    if (oldVersion.isEmpty() || oldHashes.size() != 4) { return 7; }
    QRemoteObjectNode node; node.connectToNode(QUrl(QStringLiteral("local:") + amnezia::getIpcServiceUrl()));
    QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
    if (!replica->waitForSource(10000)) { return 7; }
    const QJsonObject guard { { "vpnServer", "192.0.2.1" }, { "hopTransportPort", 443 }, { "killSwitchOption", true },
        { "splitTunnelType", 0 }, { "dns1", "9.9.9.9" } };
    auto protection = replica->applyTrafficGuard(guard, QString());
    if (!protection.waitForFinished(5000) || !protection.returnValue()) { return 7; }
    const auto filtersBefore = ownProtectionFiltersInVm();
    if (filtersBefore <= 0) { return 7; }
    // Снятие тестовой защиты выполняется и при отрицательном результате проверки.
    const auto clearProtection = [&]() {
        QProcess recover; recover.start(directory + "/hop_guard_recover.exe", { "--vm-test-only", "--vm-test-guard-only" });
        if (!recover.waitForFinished(10000)) { recover.kill(); recover.waitForFinished(2000); }
        const auto remaining = ownProtectionFiltersInVm();
        output << "Installer test protection cleanup: " << recover.exitCode() << "/" << remaining << Qt::endl;
        return recover.exitStatus() == QProcess::NormalExit && recover.exitCode() == 0 && remaining == 0;
    };
    auto cleanup = qScopeGuard([&]() { clearProtection(); });
    replica.clear();
    const auto log = QDir::tempPath() + QStringLiteral("/Hop-MSI-%1.log").arg(phase);
    QProcess process; process.setProgram(QStringLiteral("C:/Windows/System32/msiexec.exe"));
    process.setNativeArguments(QStringLiteral("/i \"%1\" /qn /norestart /L*V \"%2\" INSTALLFOLDER=\"%3\"")
        .arg(QDir::toNativeSeparators(package), QDir::toNativeSeparators(log), QDir::toNativeSeparators(destination)));
    process.start(); const bool done = process.waitForFinished(90000);
    if (!done) { process.kill(); process.waitForFinished(3000); }
    QFile::copy(log, QStringLiteral("//VBOXSVR/HopTestResults/HopJobs/Hop-MSI-%1-%2.log").arg(phase).arg(QCoreApplication::applicationPid()));
    const auto after = installedHopProducts();
    const bool oneProduct = after.size() == 1;
    const auto newVersion = oneProduct ? installedHopVersion(after.first()) : QString();
    const bool retained = ownProtectionFiltersInVm() == filtersBefore;
    const bool restored = after == before && newVersion == oldVersion && fileHashes() == oldHashes;
    const bool upgraded = oneProduct && after != before
        && QVersionNumber::fromString(newVersion) > QVersionNumber::fromString(oldVersion);
    bool serviceReady = false;
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (serviceMatches(true)) { serviceReady = true; break; }
        QThread::msleep(100);
    }
    output << "MSI " << phase << " result: " << (done ? process.exitCode() : -1) << Qt::endl;
    output << "MSI versions before/after: " << oldVersion << "/" << newVersion << "; related products: " << after.size() << Qt::endl;
    output << "MSI owned service ready/protection retained/files restored: " << serviceReady << "/" << retained << "/" << restored << Qt::endl;
    if (!done || !oneProduct || !serviceReady || !retained) { return 7; }
    bool accepted = (process.exitCode() == 0 || process.exitCode() == 3010) && upgraded;
    if (rollback || downgrade) {
        QFile detail(log); if (!detail.open(QIODevice::ReadOnly) || detail.size() > 16 * 1024 * 1024) { return 7; }
        const auto bytes = detail.readAll();
        const auto text = bytes.startsWith("\xff\xfe") ? QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2), (bytes.size() - 2) / 2) : QString::fromLocal8Bit(bytes);
        const bool expectedFailure = text.contains(rollback ? QStringLiteral("HOP_TEST_ROLLBACK_AFTER_EXECUTE") : QStringLiteral("WIX_DOWNGRADE_DETECTED"));
        accepted = process.exitCode() == 1603 && restored && expectedFailure;
    }
    cleanup.dismiss();
    return clearProtection() && accepted ? 0 : 7;
}
