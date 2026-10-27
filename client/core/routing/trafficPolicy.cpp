#include "trafficPolicy.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QUrl>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QSet>

namespace hop {
namespace {
bool validTarget(const QString &target) {
    return target == QStringLiteral("vpn") || target == QStringLiteral("direct") || target == QStringLiteral("block");
}
QString tag(const QString &target) { return QStringLiteral("hop-") + target; }
bool validRegularExpressions(const QJsonArray &rules) {
    QJsonArray patterns; QSet<QString> seen;
    for (const auto &value : rules) {
        for (const auto &domain : value.toObject().value(QStringLiteral("domain")).toArray()) {
            const auto pattern = domain.toString();
            if (pattern.startsWith(QStringLiteral("regexp:")) && !seen.contains(pattern)) {
                patterns.append(pattern); seen.insert(pattern);
            }
        }
    }
    if (patterns.isEmpty()) { return true; }
    // Go runtime ядра изолирован от runtime agw в GUI отдельным короткоживущим процессом.
    QProcess checker;
    checker.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("hop_endpoint_probe.exe")));
    checker.setArguments({ QStringLiteral("--validate-domain-patterns") });
    checker.start();
    if (!checker.waitForStarted(2000)) { return false; }
    checker.write(QJsonDocument(patterns).toJson(QJsonDocument::Compact)); checker.closeWriteChannel();
    if (!checker.waitForFinished(10000)) { checker.kill(); checker.waitForFinished(2000); return false; }
    return checker.exitStatus() == QProcess::NormalExit && checker.exitCode() == 0
        && QJsonDocument::fromJson(checker.readAllStandardOutput()).object().value(QStringLiteral("valid")) == true;
}
QString domainPattern(const QString &text) {
    if (text.size() > 4096 || text.contains(QChar::Null) || text.trimmed() != text) { return {}; }
    if (text.startsWith(QStringLiteral("regexp:"))) { return text.size() > 7 ? text : QString(); }
    if (text.startsWith(QStringLiteral("keyword:"))) { return text.size() > 8 ? text : QString(); }
    auto domain = text;
    QString prefix = QStringLiteral("domain:");
    for (const auto &kind : { QStringLiteral("domain:"), QStringLiteral("full:") }) {
        if (domain.startsWith(kind)) { domain.remove(0, kind.size()); prefix = kind; break; }
    }
    if (domain.endsWith(QLatin1Char('.'))) { domain.chop(1); }
    if (domain.isEmpty() || domain.contains(QLatin1Char('/')) || domain.contains(QLatin1Char(':'))
        || domain.contains(QLatin1Char('@')) || domain.contains(QLatin1Char('*'))) { return {}; }
    const auto ace = QUrl::toAce(domain.toLower());
    if (ace.isEmpty() || ace.size() > 253) { return {}; }
    for (const auto &label : ace.split('.')) {
        if (label.isEmpty() || label.size() > 63 || label.startsWith('-') || label.endsWith('-')) { return {}; }
        for (const auto value : label) {
            if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-')) { return {}; }
        }
    }
    return prefix + QString::fromLatin1(ace);
}
}

CompiledTrafficPolicy compileTrafficPolicy(const QJsonObject &policy, const GeoDataSet *geoIp, const GeoDataSet *geoSite)
{
    CompiledTrafficPolicy result;
    const auto fail = [&](const QString &message) { result.error = message; result.rules = {}; return result; };
    const auto defaultValue = policy.value(QStringLiteral("default"));
    if (!defaultValue.isUndefined() && !defaultValue.isString()) { return fail(QCoreApplication::translate("HopRoutingError", "Некорректное направление по умолчанию")); }
    result.defaultTarget = defaultValue.toString(QStringLiteral("vpn"));
    if (!validTarget(result.defaultTarget)) { return fail(QCoreApplication::translate("HopRoutingError", "Неизвестное направление по умолчанию")); }
    const auto list = policy.value(QStringLiteral("rules"));
    if ((!list.isUndefined() && !list.isArray()) || list.toArray().size() > 512
        || QJsonDocument(policy).toJson(QJsonDocument::Compact).size() > 2 * 1024 * 1024) {
        return fail(QCoreApplication::translate("HopRoutingError", "Некорректная или слишком большая политика маршрутов"));
    }
    result.needsDirect = result.defaultTarget == QStringLiteral("direct");
    qsizetype compiledBytes = 0;
    for (const auto &value : list.toArray()) {
        if (!value.isObject()) { return fail(QCoreApplication::translate("HopRoutingError", "Некорректное правило маршрутов")); }
        const auto rule = value.toObject();
        const auto kind = rule.value(QStringLiteral("kind")).toString();
        const auto target = rule.value(QStringLiteral("target")).toString();
        const auto match = rule.value(QStringLiteral("value")).toString();
        if (!validTarget(target) || match.isEmpty()) { return fail(QCoreApplication::translate("HopRoutingError", "Укажите правило и направление трафика")); }
        QJsonObject output { { QStringLiteral("type"), QStringLiteral("field") }, { QStringLiteral("outboundTag"), tag(target) } };
        if (kind == QStringLiteral("geoip") || kind == QStringLiteral("geosite")) {
            const auto *data = kind == QStringLiteral("geoip") ? geoIp : geoSite;
            if (!data) { return fail(QCoreApplication::translate("HopRoutingError", "Загрузите DAT для выбранной категории")); }
            QString error;
            output = geoCategoryXrayRule(*data, match, tag(target), &error);
            if (!error.isEmpty()) { return fail(error); }
            if (kind == QStringLiteral("geosite")) {
                QJsonArray domains;
                for (const auto &value : output.value(QStringLiteral("domain")).toArray()) {
                    const auto pattern = domainPattern(value.toString());
                    if (pattern.isEmpty()) { return fail(QCoreApplication::translate("HopRoutingError", "Категория DAT содержит некорректный домен или шаблон")); }
                    domains.append(pattern);
                }
                output.insert(QStringLiteral("domain"), domains);
            }
            result.hasDomains |= kind == QStringLiteral("geosite");
        } else if (kind == QStringLiteral("ip")) {
            const auto prefix = QHostAddress::parseSubnet(match);
            if (prefix.first.isNull() || prefix.second < 0 || match.contains(QLatin1Char('%'))) {
                return fail(QCoreApplication::translate("HopRoutingError", "Некорректный IP или CIDR"));
            }
            output.insert(QStringLiteral("ip"), QJsonArray { prefix.first.toString() + QLatin1Char('/') + QString::number(prefix.second) });
        } else if (kind == QStringLiteral("domain")) {
            const auto pattern = domainPattern(match);
            if (pattern.isEmpty()) { return fail(QCoreApplication::translate("HopRoutingError", "Некорректный домен или шаблон")); }
            output.insert(QStringLiteral("domain"), QJsonArray { pattern });
            result.hasDomains = true;
        } else { return fail(QCoreApplication::translate("HopRoutingError", "Неизвестный тип правила")); }
        compiledBytes += QJsonDocument(output).toJson(QJsonDocument::Compact).size();
        if (compiledBytes > 32 * 1024 * 1024) { return fail(QCoreApplication::translate("HopRoutingError", "Выбранные категории превышают 32 МиБ правил")); }
        result.needsDirect |= target == QStringLiteral("direct");
        result.rules.append(output);
    }
    if (!validRegularExpressions(result.rules)) { return fail(QCoreApplication::translate("HopRoutingError", "Некорректное регулярное выражение или недоступна его проверка")); }
    return result;
}

QJsonObject serializeTrafficPolicy(const CompiledTrafficPolicy &policy)
{
    if (!policy.isValid()) { return {}; }
    return { { QStringLiteral("default"), policy.defaultTarget }, { QStringLiteral("rules"), policy.rules },
        { QStringLiteral("hasDomains"), policy.hasDomains }, { QStringLiteral("needsDirect"), policy.needsDirect } };
}
CompiledTrafficPolicy deserializeTrafficPolicy(const QJsonObject &snapshot)
{
    CompiledTrafficPolicy result;
    result.defaultTarget = snapshot.value(QStringLiteral("default")).toString();
    const auto rules = snapshot.value(QStringLiteral("rules"));
    if (!validTarget(result.defaultTarget) || !rules.isArray() || rules.toArray().size() > 512
        || QJsonDocument(snapshot).toJson(QJsonDocument::Compact).size() > 32 * 1024 * 1024 + 65536) {
        result.error = QCoreApplication::translate("HopRoutingError", "Некорректный снимок политики"); return result;
    }
    result.needsDirect = result.defaultTarget == QStringLiteral("direct");
    for (const auto &value : rules.toArray()) {
        const auto rule = value.toObject();
        const auto output = rule.value(QStringLiteral("outboundTag")).toString();
        const auto domains = rule.value(QStringLiteral("domain")), ips = rule.value(QStringLiteral("ip"));
        if (rule.size() != 3 || rule.value(QStringLiteral("type")) != QStringLiteral("field")
            || (output != QStringLiteral("hop-vpn") && output != QStringLiteral("hop-direct") && output != QStringLiteral("hop-block"))
            || (domains.isArray() == ips.isArray()) || (domains.isArray() ? domains.toArray() : ips.toArray()).isEmpty()) {
            result.error = QCoreApplication::translate("HopRoutingError", "Некорректное правило снимка"); result.rules = {}; return result;
        }
        for (const auto &entry : (domains.isArray() ? domains.toArray() : ips.toArray())) {
            if (!entry.isString() || entry.toString().size() > 4096
                || (domains.isArray() ? domainPattern(entry.toString()).isEmpty() : QHostAddress::parseSubnet(entry.toString()).second < 0)) {
                result.error = QCoreApplication::translate("HopRoutingError", "Некорректное значение снимка"); result.rules = {}; return result;
            }
        }
        result.hasDomains |= domains.isArray(); result.needsDirect |= output == QStringLiteral("hop-direct"); result.rules.append(rule);
    }
    if (!validRegularExpressions(result.rules)) {
        result.error = QCoreApplication::translate("HopRoutingError", "Некорректное регулярное выражение снимка или недоступна его проверка"); result.rules = {};
    }
    return result;
}

QJsonObject applyXrayTrafficPolicy(const QJsonObject &base, const CompiledTrafficPolicy &policy,
                                  const QString &dnsAddress, QString *error)
{
    if (error) { error->clear(); }
    const auto fail = [error](const QString &message) { if (error) { *error = message; } return QJsonObject(); };
    const QHostAddress dns(dnsAddress);
    if (!policy.isValid() || dns.isNull() || dns.isLoopback() || dns.isMulticast() || dns.isBroadcast()
        || dnsAddress.contains(QLatin1Char('%')) || dnsAddress.contains(QLatin1Char('/'))) {
        return fail(QCoreApplication::translate("HopRoutingError", "Нет корректной политики или DNS VPN"));
    }
    QJsonObject vpn;
    for (const auto &value : base.value(QStringLiteral("outbounds")).toArray()) {
        const auto outbound = value.toObject();
        if (outbound.value(QStringLiteral("protocol")) == QStringLiteral("vless")
            || (outbound.value(QStringLiteral("tag")) == QStringLiteral("hop-vpn")
                && outbound.value(QStringLiteral("protocol")) == QStringLiteral("freedom"))) { vpn = outbound; break; }
    }
    if (vpn.isEmpty()) { return fail(QCoreApplication::translate("HopRoutingError", "Не найден выход VPN для политики маршрутов")); }
    vpn.insert(QStringLiteral("tag"), QStringLiteral("hop-vpn"));
    if (vpn.value(QStringLiteral("protocol")) == QStringLiteral("freedom")) {
        auto settings = vpn.value(QStringLiteral("settings")).toObject();
        settings.insert(QStringLiteral("domainStrategy"), QStringLiteral("ForceIPv4"));
        vpn.insert(QStringLiteral("settings"), settings);
    }

    auto result = base;
    result.insert(QStringLiteral("hopPolicy"), QJsonObject { { QStringLiteral("needsDirect"), policy.needsDirect } });
    auto inbounds = base.value(QStringLiteral("inbounds")).toArray();
    if (inbounds.isEmpty()) { return fail(QCoreApplication::translate("HopRoutingError", "Нет входа TUN/SOCKS для политики")); }
    for (int index = 0; index < inbounds.size(); ++index) {
        auto inbound = inbounds.at(index).toObject();
        inbound.insert(QStringLiteral("sniffing"), QJsonObject {
            { QStringLiteral("enabled"), true }, { QStringLiteral("metadataOnly"), false },
            { QStringLiteral("destOverride"), QJsonArray { QStringLiteral("fakedns"), QStringLiteral("http"), QStringLiteral("tls"), QStringLiteral("quic") } } });
        inbounds[index] = inbound;
    }
    result.insert(QStringLiteral("inbounds"), inbounds);
    result.insert(QStringLiteral("log"), QJsonObject { { QStringLiteral("loglevel"), QStringLiteral("none") } });
    auto direct = QJsonObject { { QStringLiteral("tag"), QStringLiteral("hop-direct") }, { QStringLiteral("protocol"), QStringLiteral("freedom") },
        { QStringLiteral("settings"), QJsonObject { { QStringLiteral("domainStrategy"), QStringLiteral("ForceIPv4") } } } };
    result.insert(QStringLiteral("outbounds"), QJsonArray { vpn, direct,
        QJsonObject { { QStringLiteral("tag"), QStringLiteral("hop-block") }, { QStringLiteral("protocol"), QStringLiteral("blackhole") } },
        QJsonObject { { QStringLiteral("tag"), QStringLiteral("hop-dns-out") }, { QStringLiteral("protocol"), QStringLiteral("dns") },
            { QStringLiteral("proxySettings"), QJsonObject { { QStringLiteral("tag"), QStringLiteral("hop-vpn") } } },
            { QStringLiteral("settings"), QJsonObject { { QStringLiteral("rewriteAddress"), dns.toString() }, { QStringLiteral("rewritePort"), 53 },
                { QStringLiteral("rules"), QJsonArray { QJsonObject { { QStringLiteral("action"), QStringLiteral("direct") },
                    { QStringLiteral("domain"), QJsonArray { QStringLiteral("full:hop-health.example.com") } } } } } } } } });
    QJsonArray servers;
    if (policy.hasDomains) { servers.append(QStringLiteral("fakedns")); }
    servers.append(dns.toString());
    result.insert(QStringLiteral("dns"), QJsonObject { { QStringLiteral("tag"), QStringLiteral("hop-dns") },
        { QStringLiteral("queryStrategy"), QStringLiteral("UseIPv4") }, { QStringLiteral("servers"), servers } });
    if (policy.hasDomains) { result.insert(QStringLiteral("fakedns"), QJsonObject {
        { QStringLiteral("ipPool"), QStringLiteral("198.18.0.0/15") }, { QStringLiteral("poolSize"), 32768 } }); }
    else { result.remove(QStringLiteral("fakedns")); }
    QJsonArray rules {
        QJsonObject { { QStringLiteral("type"), QStringLiteral("field") }, { QStringLiteral("inboundTag"), QJsonArray { QStringLiteral("hop-dns") } },
            { QStringLiteral("outboundTag"), QStringLiteral("hop-vpn") } },
        QJsonObject { { QStringLiteral("type"), QStringLiteral("field") }, { QStringLiteral("port"), QStringLiteral("53") },
            { QStringLiteral("outboundTag"), QStringLiteral("hop-dns-out") } }
    };
    for (const auto &rule : policy.rules) { rules.append(rule); }
    rules.append(QJsonObject { { QStringLiteral("type"), QStringLiteral("field") }, { QStringLiteral("network"), QStringLiteral("tcp,udp") },
        { QStringLiteral("outboundTag"), tag(policy.defaultTarget) } });
    result.insert(QStringLiteral("routing"), QJsonObject { { QStringLiteral("domainStrategy"), QStringLiteral("IPOnDemand") },
        { QStringLiteral("rules"), rules } });
    return result;
}
} // namespace hop
