#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace hop {

constexpr qsizetype maximumSubscriptionBytes = 4 * 1024 * 1024;
constexpr qsizetype maximumSubscriptionEntries = 4096;

struct SubscriptionEntry {
    QString uri;
    QString name;
    QString connectionKey;
    QString nameKey;
};

struct SubscriptionParseResult {
    QVector<SubscriptionEntry> entries;
    QString error;
    bool isValid() const { return error.isEmpty() && !entries.isEmpty(); }
};

SubscriptionParseResult parseSubscription(const QByteArray &body);

struct PreviousSubscriptionEntry {
    QString serverId;
    QString connectionKey;
    QString nameKey;
};

struct SubscriptionMatchResult {
    QVector<QString> serverIds;
    QString error;
};

SubscriptionMatchResult matchSubscriptionEntries(const QVector<SubscriptionEntry> &incoming,
                                                  const QVector<PreviousSubscriptionEntry> &previous);

} // namespace hop
