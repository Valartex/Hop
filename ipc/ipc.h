#ifndef IPC_H
#define IPC_H

#include <QObject>
#include <QString>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include "../client/core/utils/utilities.h"

#include "version.h"

#define IPC_SERVICE_URL APP_IPC_SERVICE_URL

namespace amnezia {

enum PermittedProcess {
    Invalid,
    OpenVPN,
    Wireguard,
    Tun2Socks,
    CertUtil,
    PermittedProcessCount
};

inline QString permittedProcessPath(PermittedProcess pid)
{
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && pid != PermittedProcess::Tun2Socks) { return {}; }
#endif
    switch (pid) {
        case PermittedProcess::OpenVPN:
            return Utils::openVpnExecPath();
        case PermittedProcess::Wireguard:
            return Utils::wireguardExecPath();
        case PermittedProcess::CertUtil:
            return Utils::certUtilPath();
        case PermittedProcess::Tun2Socks:
            return Utils::tun2socksPath();
        default:
            return "";
    }
}


inline QString getIpcServiceUrl() {
#ifdef Q_OS_WIN
    return IPC_SERVICE_URL;
#else
    return QString("/tmp/%1").arg(IPC_SERVICE_URL);
#endif
}

inline QString getIpcProcessUrl(int pid) {
#ifdef Q_OS_WIN
    return QString("%1_%2").arg(IPC_SERVICE_URL).arg(pid);
#else
    return QString("/tmp/%1_%2").arg(IPC_SERVICE_URL).arg(pid);
#endif
}

inline QStringList sanitizeArguments(PermittedProcess proc, const QStringList &args) {
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
        if (proc != Tun2Socks || args.size() != 4 || args.at(0) != QStringLiteral("-device")
            || args.at(1) != QStringLiteral("tun://") + QStringLiteral(APP_WINDOWS_XRAY_INTERFACE)
            || args.at(2) != QStringLiteral("-proxy") || args.at(3).size() > 4096) { return {}; }
        const QUrl proxy(args.at(3), QUrl::StrictMode);
        if (!proxy.isValid() || proxy.scheme() != QStringLiteral("socks5") || proxy.host() != QStringLiteral("127.0.0.1")
            || proxy.port() < 1 || proxy.port() > 65535 || !proxy.path().isEmpty() || proxy.hasQuery() || proxy.hasFragment()) { return {}; }
        return args;
    }
#endif
    using Validator = std::function<bool(const QString&)>;
    QMap<QString, Validator> namedArgs;
    QList<Validator> positionalArgs;

    switch (proc) {
    case OpenVPN: {
        namedArgs["--config"] = [](const QString& v) { return !v.isEmpty(); };
        namedArgs["--management"] = [](const QString& v) { return !v.isEmpty(); };
        namedArgs["--management-client"] = nullptr;
        positionalArgs.append([](const QString& v) {
            bool ok;
            int port = v.toInt(&ok);
            return ok && port > 0 && port <= 65535;
        });
        break;
    }
    case Tun2Socks:
        namedArgs["-device"] = [](const QString& v) { return v.startsWith("tun://"); };
        namedArgs["-proxy"] = [](const QString& v) { return v.startsWith("socks5://"); };
        break;
    case CertUtil:
        return args;
    default:
        return {};
    }

    QStringList sanitized;

    for (int i = 0, pos = 0; i < args.size(); i++) {
        const auto& key = args[i];

        if (const auto found = namedArgs.find(key); found != namedArgs.end()) {
            const auto validator = found.value();

            if (validator) {
                if (i + 1 < args.size()) {
                    const auto& value = args[i+1];
                    if (validator(value)) {
                        sanitized << key << value;
                        i++;
                    }
                }
            } else {
                sanitized << key;
            }
        } else if (pos < positionalArgs.size()) {
            if (const auto validator = positionalArgs[pos]; validator && validator(key)) {
                sanitized << key;
                pos++;
            }
        }
    }

    return sanitized;
}

} // namespace amnezia

#endif // IPC_H
