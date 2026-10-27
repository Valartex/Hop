#pragma once

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hop {

using Clock = std::chrono::steady_clock;

enum class Reachability { Unknown, Reachable, Unreachable };

struct Location {
    std::string id;
    std::string serviceId;
    std::string countryCode;
    bool enabled = true;
};

struct ProbeResult {
    Reachability reachability = Reachability::Unknown;
    Clock::time_point sampledAt {};
    std::chrono::milliseconds latency { 0 };
};

enum class PoolEntryKind { Location, Country };

struct PoolEntry {
    PoolEntryKind kind;
    std::string key;
};

struct SelectionPolicy {
    std::optional<std::string> pinnedLocation;
    std::vector<PoolEntry> priorities;
    std::unordered_set<std::string> excludedLocations;
    std::chrono::seconds maximumProbeAge { 120 };
};

enum class SelectionReason { Pinned, KeepCurrent, Priority, NoCandidate, PinnedUnavailable };

struct SelectionDecision {
    std::optional<std::string> locationId;
    SelectionReason reason;
};

inline std::string normalizedCountry(std::string country)
{
    // ISO-код берётся из метаданных провайдера; географию по имени не угадываем.
    for (char &character : country) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return country;
}

inline bool isFreshAndReachable(const ProbeResult &probe,
                                Clock::time_point now,
                                std::chrono::seconds maximumAge)
{
    return maximumAge.count() > 0
        && probe.reachability == Reachability::Reachable
        && probe.latency.count() >= 0
        && probe.sampledAt <= now
        && now - probe.sampledAt < maximumAge;
}

inline SelectionDecision selectLocation(
    const std::vector<Location> &locations,
    const std::unordered_map<std::string, ProbeResult> &probes,
    const SelectionPolicy &policy,
    const std::optional<std::string> &currentLocation,
    Clock::time_point now)
{
    std::unordered_map<std::string, const Location *> byId;
    for (const auto &location : locations) {
        if (location.id.empty() || !byId.emplace(location.id, &location).second) {
            throw std::invalid_argument("Location IDs must be nonempty and unique");
        }
    }

    const auto usable = [&](const Location &location) {
        const auto probe = probes.find(location.id);
        return location.enabled
            && policy.excludedLocations.count(location.id) == 0
            && probe != probes.end()
            && isFreshAndReachable(probe->second, now, policy.maximumProbeAge);
    };

    if (policy.pinnedLocation) {
        const auto found = byId.find(*policy.pinnedLocation);
        if (found != byId.end() && usable(*found->second)) {
            return { found->first, SelectionReason::Pinned };
        }
        return { std::nullopt, SelectionReason::PinnedUnavailable };
    }

    std::vector<const Location *> candidates;
    std::unordered_set<std::string> added;
    for (const auto &entry : policy.priorities) {
        std::vector<const Location *> group;
        if (entry.kind == PoolEntryKind::Location) {
            const auto found = byId.find(entry.key);
            if (found != byId.end() && usable(*found->second)) {
                group.push_back(found->second);
            }
        } else {
            const auto country = normalizedCountry(entry.key);
            // Неизвестная страна не образует автоматический пул.
            if (country.size() != 2 || country[0] < 'A' || country[0] > 'Z'
                || country[1] < 'A' || country[1] > 'Z') {
                continue;
            }
            for (const auto &location : locations) {
                if (normalizedCountry(location.countryCode) == country && usable(location)) {
                    group.push_back(&location);
                }
            }
            std::sort(group.begin(), group.end(), [&](const Location *left, const Location *right) {
                const auto leftLatency = probes.at(left->id).latency;
                const auto rightLatency = probes.at(right->id).latency;
                // При одинаковой задержке сохраняем детерминированный порядок.
                return leftLatency != rightLatency ? leftLatency < rightLatency : left->id < right->id;
            });
        }
        for (const auto *location : group) {
            if (added.insert(location->id).second) {
                candidates.push_back(location);
            }
        }
    }

    // Работающий разрешённый узел не меняем ради более низкой задержки другого узла.
    if (currentLocation && added.count(*currentLocation) != 0) {
        return { currentLocation, SelectionReason::KeepCurrent };
    }
    if (!candidates.empty()) {
        return { candidates.front()->id, SelectionReason::Priority };
    }
    return { std::nullopt, SelectionReason::NoCandidate };
}

class FailureGate {
public:
    explicit FailureGate(unsigned int requiredFailures = 3,
                         std::chrono::seconds switchCooldown = std::chrono::seconds(30))
        : m_requiredFailures(requiredFailures), m_switchCooldown(switchCooldown)
    {
        if (requiredFailures == 0 || switchCooldown.count() < 0) {
            throw std::invalid_argument("Invalid failure gate settings");
        }
    }

    bool observe(bool tunnelHealthy, bool networkOnline, Clock::time_point now)
    {
        if (tunnelHealthy || !networkOnline) {
            m_failures = 0;
            return false;
        }
        // Счётчик ограничен порогом, чтобы не переполниться при долгом отказе.
        if (m_failures < m_requiredFailures) {
            ++m_failures;
        }
        return m_failures >= m_requiredFailures
            && (!m_lastSwitch || (now >= *m_lastSwitch && now - *m_lastSwitch >= m_switchCooldown));
    }

    void switched(Clock::time_point now)
    {
        m_lastSwitch = now;
        m_failures = 0;
    }

private:
    unsigned int m_requiredFailures;
    std::chrono::seconds m_switchCooldown;
    unsigned int m_failures = 0;
    std::optional<Clock::time_point> m_lastSwitch;
};

} // namespace hop
