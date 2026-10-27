#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QThread>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

// Дополнительный ключ передаётся в тестовую VM по одноразовому RSA-OAEP каналу.
// Приватный RSA-ключ не покидает процесс; общая папка содержит только public key и шифротекст.
inline QString extraVmInput(bool &requested)
{
    QFile marker(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop-VM-Extra.json"));
    requested = marker.exists();
    if (!requested || !marker.open(QIODevice::ReadOnly) || marker.size() > 4096) { return {}; }
    const auto request = QJsonDocument::fromJson(marker.readAll()).object();
    marker.close();
    marker.remove();
    const auto nonce = request.value(QStringLiteral("nonce")).toString();
    const auto expiry = QDateTime::fromString(request.value(QStringLiteral("expires")).toString(), Qt::ISODateWithMs);
    const auto remaining = QDateTime::currentDateTimeUtc().msecsTo(expiry);
    if (!QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match(nonce).hasMatch()
        || request.value(QStringLiteral("kind")).toString() != QStringLiteral("amnezia-awg")
        || !expiry.isValid() || remaining < 0 || remaining > 600000) { return {}; }
    auto *generation = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!generation) { return {}; }
    const auto freeGeneration = qScopeGuard([&]() { EVP_PKEY_CTX_free(generation); });
    EVP_PKEY *key = nullptr;
    if (EVP_PKEY_keygen_init(generation) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(generation, 4096) <= 0
        || EVP_PKEY_keygen(generation, &key) <= 0) { return {}; }
    const auto freeKey = qScopeGuard([&]() { EVP_PKEY_free(key); });
    auto *bio = BIO_new(BIO_s_mem());
    if (!bio) { return {}; }
    const auto freeBio = qScopeGuard([&]() { BIO_free(bio); });
    if (PEM_write_bio_PUBKEY(bio, key) <= 0) { return {}; }
    char *publicBytes = nullptr;
    const auto publicSize = BIO_get_mem_data(bio, &publicBytes);
    const auto root = QStringLiteral("//VBOXSVR/HopTestResults/HopJobs/") + nonce;
    QSaveFile publicFile(root + QStringLiteral("-key.json"));
    const QByteArray publicJson = QJsonDocument(QJsonObject {
        { QStringLiteral("nonce"), nonce },
        { QStringLiteral("public_key_pem"), QString::fromUtf8(publicBytes, publicSize) }
    }).toJson(QJsonDocument::Compact);
    if (!publicFile.open(QIODevice::WriteOnly) || publicFile.write(publicJson) != publicJson.size() || !publicFile.commit()) { return {}; }
    QFile encryptedFile(root + QStringLiteral("-data.json"));
    QElapsedTimer deadline;
    deadline.start();
    while (!encryptedFile.exists() && deadline.elapsed() < 30000) { QThread::msleep(100); }
    if (!encryptedFile.open(QIODevice::ReadOnly) || encryptedFile.size() > 32768) { return {}; }
    const auto envelope = QJsonDocument::fromJson(encryptedFile.readAll()).object();
    encryptedFile.close();
    encryptedFile.remove();
    if (envelope.value(QStringLiteral("nonce")).toString() != nonce) { return {}; }
    const auto encrypted = QByteArray::fromBase64(envelope.value(QStringLiteral("encrypted")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (encrypted.size() != 512) { return {}; }
    auto *decryption = EVP_PKEY_CTX_new(key, nullptr);
    if (!decryption) { return {}; }
    const auto freeDecryption = qScopeGuard([&]() { EVP_PKEY_CTX_free(decryption); });
    if (EVP_PKEY_decrypt_init(decryption) <= 0 || EVP_PKEY_CTX_set_rsa_padding(decryption, RSA_PKCS1_OAEP_PADDING) <= 0
        || EVP_PKEY_CTX_set_rsa_oaep_md(decryption, EVP_sha256()) <= 0
        || EVP_PKEY_CTX_set_rsa_mgf1_md(decryption, EVP_sha256()) <= 0) { return {}; }
    QByteArray plain(512, Qt::Uninitialized);
    const auto wipe = qScopeGuard([&]() { OPENSSL_cleanse(plain.data(), static_cast<size_t>(plain.size())); });
    size_t size = static_cast<size_t>(plain.size());
    if (EVP_PKEY_decrypt(decryption, reinterpret_cast<unsigned char *>(plain.data()), &size,
                         reinterpret_cast<const unsigned char *>(encrypted.constData()), static_cast<size_t>(encrypted.size())) <= 0
        || size == 0 || size > 446) { return {}; }
    if (!envelope.contains(QStringLiteral("body"))) { return QString::fromUtf8(plain.constData(), static_cast<qsizetype>(size)); }
    if (size != 32) { return {}; }
    const auto decode = [&](const char *field) { return QByteArray::fromBase64(envelope.value(QLatin1String(field)).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors); };
    const auto iv = decode("iv");
    auto tag = decode("tag");
    const auto body = decode("body");
    if (iv.size() != 12 || tag.size() != 16 || body.isEmpty() || body.size() > 16384) { return {}; }
    auto *cipher = EVP_CIPHER_CTX_new();
    if (!cipher) { return {}; }
    const auto freeCipher = qScopeGuard([&]() { EVP_CIPHER_CTX_free(cipher); });
    const auto aad = nonce.toUtf8();
    QByteArray decoded(body.size() + 16, Qt::Uninitialized);
    const auto wipeDecoded = qScopeGuard([&]() { OPENSSL_cleanse(decoded.data(), static_cast<size_t>(decoded.size())); });
    int length = 0, finalLength = 0;
    if (EVP_DecryptInit_ex(cipher, EVP_aes_256_gcm(), nullptr, reinterpret_cast<const unsigned char *>(plain.constData()),
                          reinterpret_cast<const unsigned char *>(iv.constData())) <= 0
        || EVP_DecryptUpdate(cipher, nullptr, &length, reinterpret_cast<const unsigned char *>(aad.constData()), static_cast<int>(aad.size())) <= 0
        || EVP_DecryptUpdate(cipher, reinterpret_cast<unsigned char *>(decoded.data()), &length,
                             reinterpret_cast<const unsigned char *>(body.constData()), static_cast<int>(body.size())) <= 0
        || EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) <= 0
        || EVP_DecryptFinal_ex(cipher, reinterpret_cast<unsigned char *>(decoded.data()) + length, &finalLength) <= 0) { return {}; }
    return QString::fromUtf8(decoded.constData(), length + finalLength);
}
