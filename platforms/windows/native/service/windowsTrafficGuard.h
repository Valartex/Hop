#pragma once

#include <QJsonObject>
#include <QString>

// Отдельный постоянный набор WFP: авария процесса не снимает защиту.
class WindowsTrafficGuard {
public:
    static bool apply(const QJsonObject &config, const QString &tunnelAlias);
    static bool clear();
    static int filterCount();
    // Регистрация контекста не создаёт разрешений. Каждый сокет получает отдельный endpoint-фильтр.
    static bool beginDirectPolicy(int physicalInterface, const QString &physicalAddress);
    static void endDirectPolicy();
    static quint64 openDirectSocket(const QString &source, const QString &destination, int port, int protocol);
    static void closeDirectSocket(quint64 lease);
    static bool permitControlEndpoint(const QString &address, int port, bool udp);
};
