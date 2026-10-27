#pragma once
#include "amneziaVmProbe.h"
#include "core/locations/endpointHealthController.h"

// Только VM: реальный AWG/DNS без системного туннеля и без выпуска API-конфигурации.
inline int probeAwgEndpointInVm(const QString &key, QTextStream &output)
{
    const auto previousLogger = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    const auto restoreLogger = qScopeGuard([&]() { qInstallMessageHandler(previousLogger); });
    SecureQSettings settings("Hop-AwgEndpoint-VM", "memory", nullptr, true, false);
    SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
    const auto imported = importer.extractConfigFromData(key);
    if (imported.errorCode != ErrorCode::NoError) { return 7; }
    servers.addServer("probe", imported.config, serverConfigUtils::ConfigType::Native);
    hop::EndpointHealthController health(&servers);
    const auto wait = [&]() {
        QElapsedTimer deadline; deadline.start();
        while (health.isBusy() && deadline.elapsed() < 16000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(10); }
        return !health.isBusy();
    };
    const auto adapters = []() { QStringList names; for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (iface.humanReadableName() == "Hop" || iface.humanReadableName() == "Hop-TUN") { names.append(iface.humanReadableName()); }
    } names.sort(); return names; };
    const auto before = adapters();
    if (!health.available()) { return 7; }
    health.checkAll();
    QJsonObject helperMetadata;
    for (auto *process : health.findChildren<QProcess *>()) {
        helperMetadata.insert("image", QFileInfo(process->program()).fileName());
        QObject::connect(process, &QProcess::readyReadStandardOutput, &health, [&,process]() {
            const auto bytes = process->peek(1024); const auto result = QJsonDocument::fromJson(bytes).object();
            if (!result.isEmpty()) {
                helperMetadata.insert("outputBytes", bytes.size()); helperMetadata.insert("ok", result.value("ok")); helperMetadata.insert("latencyMs", result.value("latencyMs"));
                const auto reason = result.value("reason").toString();
                if (QRegularExpression("^[a-zA-Z ]{0,20}$").match(reason).hasMatch()) { helperMetadata.insert("reason", reason); }
                const auto statistics = result.value("statistics").toObject(); QJsonObject numbers;
                for (const auto *name : { "transportErrors", "transportCode", "rx_bytes", "tx_bytes", "last_handshake_time_sec" }) { numbers.insert(name, statistics.value(name).toInteger()); }
                helperMetadata.insert("statistics", numbers);
            }
        });
        QObject::connect(process, &QProcess::finished, &health, [&](int code,QProcess::ExitStatus status) {
            helperMetadata.insert("exit", code); helperMetadata.insert("normalExit", status == QProcess::NormalExit);
        });
    }
    if (!wait()) { return 7; }
    const auto positive = health.statuses().value("probe").toMap();
    output << "AWG candidate encrypted DNS status/latency: " << positive.value("state").toString() << "/" << positive.value("latencyMs").toInt() << Qt::endl;
    if (positive.value("state") != "reachable") {
        output << "AWG candidate controller helper metadata: " << QJsonDocument(helperMetadata).toJson(QJsonDocument::Compact) << Qt::endl;
        const auto native = servers.nativeConfig("probe");
        if (native) {
            const auto container = native->containerConfig(native->defaultContainer);
            const auto *awg = container.getAwgProtocolConfig();
            if (awg && awg->clientConfig) {
                QProcess diagnostic; diagnostic.start(QCoreApplication::applicationDirPath() + "/hop_awg_probe.exe", {});
                if (diagnostic.waitForStarted(3000)) {
                    diagnostic.write(QJsonDocument(QJsonObject { { "config", awg->clientConfig->nativeConfig } }).toJson(QJsonDocument::Compact)); diagnostic.closeWriteChannel();
                    if (!diagnostic.waitForFinished(14000)) { diagnostic.kill(); diagnostic.waitForFinished(1000); }
                    const auto response = QJsonDocument::fromJson(diagnostic.readAllStandardOutput().left(1024)).object();
                    const auto reason = response.value("reason").toString();
                    output << "AWG candidate failure stage: " << (QRegularExpression("^[a-zA-Z ]{0,20}$").match(reason).hasMatch() ? reason : QStringLiteral("invalid")) << "; exit=" << diagnostic.exitCode() << Qt::endl;
                    const auto statistics = response.value("statistics").toObject();
                    output << "AWG candidate transport error count/code, rx/tx/handshake: " << statistics.value("transportErrors").toInteger() << "/" << statistics.value("transportCode").toInteger()
                        << "/" << statistics.value("rx_bytes").toInteger() << "/" << statistics.value("tx_bytes").toInteger() << "/" << statistics.value("last_handshake_time_sec").toInteger() << Qt::endl;
                }
            }
        }
        return 7;
    }
    health.setTunnelActive(true); health.checkAll();
    const bool deferred = !health.isBusy() && health.statuses().value("probe").toMap().value("state") == "deferred";
    output << "AWG candidate deferred while VPN active: " << deferred << Qt::endl;
    health.setTunnelActive(false);
    const int port = QRegularExpression(QStringLiteral("^# HopDirectPort=(\\d{1,5})$"), QRegularExpression::MultilineOption).match(key).captured(1).toInt();
    if (port < 1 || port > 65535) { return 7; }
    QNetworkAccessManager network; network.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest pause(QUrl(QStringLiteral("http://10.0.2.2:%1/pause-dns").arg(port))); pause.setTransferTimeout(5000);
    auto *reply = network.get(pause); QElapsedTimer deadline; deadline.start();
    while (!reply->isFinished() && deadline.elapsed() < 6000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(10); }
    const bool paused = reply->isFinished() && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 204;
    if (!paused) { reply->abort(); reply->deleteLater(); return 7; } reply->deleteLater();
    health.checkAll();
    if (!wait()) { return 7; }
    const auto negative = health.statuses().value("probe").toMap().value("state").toString();
    const bool unchanged = before == adapters();
    output << "AWG candidate with silent DNS: " << negative << "; owned adapters unchanged=" << unchanged << Qt::endl;
    return deferred && negative == "unreachable" && unchanged ? 0 : 7;
}
