#include "locationSelectionPolicy.h"

#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;

namespace {
void require(bool condition, const char *scenario)
{
    if (!condition) {
        throw std::runtime_error(scenario);
    }
}
}

int main()
{
    try {
        const auto now = hop::Clock::time_point(1000s);
        const std::vector<hop::Location> locations {
            { "a-de", "provider-a", "DE" },
            { "b-de", "provider-b", "de" },
            { "b-nl", "provider-b", "NL" },
            { "disabled", "provider-a", "DE", false },
            { "unknown-country", "provider-a", "" }
        };
        std::unordered_map<std::string, hop::ProbeResult> probes {
            { "a-de", { hop::Reachability::Reachable, now, 60ms } },
            { "b-de", { hop::Reachability::Reachable, now, 20ms } },
            { "b-nl", { hop::Reachability::Reachable, now, 10ms } },
            { "disabled", { hop::Reachability::Reachable, now, 1ms } },
            { "unknown-country", { hop::Reachability::Reachable, now, 1ms } }
        };
        hop::SelectionPolicy policy;
        policy.priorities = {
            { hop::PoolEntryKind::Country, "de" },
            { hop::PoolEntryKind::Country, "NL" }
        };
        auto choose = [&]() { return hop::selectLocation(locations, probes, policy, std::nullopt, now); };
        require(choose().locationId == "b-de", "Country priority must beat faster later country");
        require(hop::selectLocation(locations, probes, policy, "a-de", now).reason
                    == hop::SelectionReason::KeepCurrent, "Healthy current location must remain selected");
        require(hop::selectLocation(locations, probes, policy, "unknown-country", now).locationId == "b-de",
                "Current location outside allowed pool must not be retained");

        policy.excludedLocations.insert("b-de");
        require(choose().locationId == "a-de", "Exclusions must override country pool");
        policy.priorities.insert(policy.priorities.begin(), { hop::PoolEntryKind::Location, "b-nl" });
        require(choose().locationId == "b-nl", "Explicit priority must override country order");
        policy.pinnedLocation = "a-de";
        require(choose().reason == hop::SelectionReason::Pinned, "Pinned location must override pool");
        probes.at("a-de").reachability = hop::Reachability::Unreachable;
        require(!choose().locationId && choose().reason == hop::SelectionReason::PinnedUnavailable,
                "Failed pinned location must never fall back");
        policy.pinnedLocation = "missing";
        require(!choose().locationId, "Removed pinned location must never fall back");
        policy.pinnedLocation.reset();
        policy.priorities = { { hop::PoolEntryKind::Country, "DE" } };
        require(!choose().locationId, "No fallback outside pool");
        probes.at("a-de") = { hop::Reachability::Reachable, now - 120s, 60ms };
        require(!choose().locationId, "TTL boundary must expire a probe");
        probes.at("a-de").sampledAt = now + 1s;
        require(!choose().locationId, "Future probe timestamp must not be accepted");
        probes.at("a-de") = { hop::Reachability::Unknown, now, 60ms };
        require(!choose().locationId, "Unknown probe must not be claimed healthy");
        probes.at("a-de") = { hop::Reachability::Reachable, now, -1ms };
        require(!choose().locationId, "Invalid latency must not be accepted");
        probes.at("a-de") = { hop::Reachability::Reachable, now, 20ms };
        policy.excludedLocations.clear();
        require(choose().locationId == "a-de", "Equal latency must use stable ID ordering");
        policy.priorities = { { hop::PoolEntryKind::Country, "" } };
        require(!choose().locationId, "Unknown country must not create a country pool");
        policy.priorities.clear();
        require(!choose().locationId, "Empty pool must fail closed");
        auto refreshed = locations;
        refreshed.push_back({ "new-de", "provider-c", "DE" });
        probes["new-de"] = { hop::Reachability::Reachable, now, 5ms };
        policy.priorities = { { hop::PoolEntryKind::Country, "DE" } };
        require(hop::selectLocation(refreshed, probes, policy, std::nullopt, now).locationId == "new-de",
                "Country pool must include new locations across services");

        bool duplicateRejected = false;
        refreshed.push_back(locations.front());
        try {
            hop::selectLocation(refreshed, probes, policy, std::nullopt, now);
        } catch (const std::invalid_argument &) {
            duplicateRejected = true;
        }
        require(duplicateRejected, "Ambiguous duplicate IDs must be rejected");

        hop::FailureGate gate;
        require(!gate.observe(false, true, now), "One failure must not switch");
        require(!gate.observe(false, true, now + 1s), "Two failures must not switch");
        require(gate.observe(false, true, now + 2s), "Three failures must trigger recovery");
        gate.switched(now + 2s);
        require(!gate.observe(false, true, now + 3s), "Switch must reset failure counter");
        gate.observe(false, true, now + 4s);
        require(!gate.observe(false, true, now + 5s), "Cooldown must suppress rapid switching");
        require(gate.observe(false, true, now + 32s), "Cooldown expiry must allow recovery");
        require(!gate.observe(false, false, now + 33s), "Offline device must not mark all servers failed");
        require(!gate.observe(false, true, now + 34s), "Offline transition must reset consecutive failures");
        require(!gate.observe(true, true, now + 35s), "Success must reset failures");
        require(!gate.observe(false, true, now + 36s), "Failure after success must start a new series");
        std::cout << "Hop location policy: all scenarios passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
