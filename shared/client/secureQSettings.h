#ifndef SECUREQSETTINGS_H
#define SECUREQSETTINGS_H

#include <QMutex>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QObject>
#include <QSettings>

#include "../client/3rd/qtkeychain/qtkeychain/keychain.h"

class SecureQSettings : public QObject
{
    Q_OBJECT

public:
    explicit SecureQSettings(const QString &organization, const QString &application = QString(),
                             QObject *parent = nullptr, bool enableEncryption = true, bool enablePersistence = true);

    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
    virtual bool setValueChecked(const QString &key, const QVariant &value);
    bool persistenceEnabled() const { return m_persistenceEnabled; }
    void remove(const QString &key);

    QByteArray backupAppConfig() const;
    bool restoreAppConfig(const QByteArray &json);

    void clearSettings();

private:
    QByteArray encryptText(const QByteArray &value) const;
    QByteArray decryptText(const QByteArray &ba) const;

    bool encryptionRequired() const;
    bool isExcludedFromBackup(const QString &key) const;

    QByteArray getEncKey() const;
    QByteArray getEncIv() const;

    static QByteArray getSecTag(const QString &tag);
    static void setSecTag(const QString &tag, const QByteArray &data);

    QSettings m_settings;

    mutable QHash<QString, QVariant> m_cache;

    QStringList encryptedKeys; // encode only key listed here
    // only this fields need for backup
    QStringList m_fieldsToBackup = {
        "Conf/", "Servers/",
    };

    QStringList m_fieldsExcludedFromBackup = {
        "Conf/installationUuid", "Conf/proxyUrls/",
    };

    mutable QByteArray m_key;
    mutable QByteArray m_iv;

    const QByteArray magicString { "EncData" }; // Magic keyword used for mark encrypted QByteArray

    bool m_encryptionEnabled;
    bool m_persistenceEnabled;

    mutable QRecursiveMutex m_mutex;
};

#endif // SECUREQSETTINGS_H
