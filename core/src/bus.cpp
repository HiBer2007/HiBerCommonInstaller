#include "hci/bus.h"

#include <algorithm>

namespace hci {

// ------------------------------------------------------------------
HandlerId EventBus::subscribe(const std::string& topic, EventHandler handler)
{
    entries_.push_back(Entry{nextId_++, topic, std::move(handler)});
    return entries_.back().id;
}

void EventBus::unsubscribe(HandlerId id)
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [id](const Entry& e) { return e.id == id; }),
                   entries_.end());
}

void EventBus::unsubscribeAll(const std::string& topic)
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&topic](const Entry& e) { return e.topic == topic; }),
                   entries_.end());
}

void EventBus::publish(const std::string& topic, const nlohmann::json& payload)
{
    if (publishing_) {
        // Re-entrant publish from a handler: deliver immediately (no queue).
        std::vector<Entry> snapshot;
        for (auto& e : entries_)
            if (e.topic == topic) snapshot.push_back(e);
        for (auto& e : snapshot)
            if (e.handler) e.handler(payload);
        return;
    }
    publishing_ = true;
    std::vector<Entry> snapshot;
    for (auto& e : entries_)
        if (e.topic == topic) snapshot.push_back(e);
    for (auto& e : snapshot) {
        if (e.handler) e.handler(payload);
    }
    publishing_ = false;
}

void EventBus::post(const std::string& topic, const nlohmann::json& payload)
{
    pending_.push_back(Pending{topic, payload});
}

void EventBus::drain()
{
    std::vector<Pending> items = std::move(pending_);
    pending_.clear();
    for (auto& p : items) publish(p.topic, p.payload);
}

// ------------------------------------------------------------------
void ServiceRegistry::registerService(const std::string& ifaceName, void* service)
{
    services_[ifaceName] = service;
}

void ServiceRegistry::unregisterService(const std::string& ifaceName)
{
    services_.erase(ifaceName);
}

void* ServiceRegistry::findService(const std::string& ifaceName) const
{
    auto it = services_.find(ifaceName);
    return it == services_.end() ? nullptr : it->second;
}

std::vector<std::string> ServiceRegistry::serviceNames() const
{
    std::vector<std::string> names;
    names.reserve(services_.size());
    for (auto& kv : services_) names.push_back(kv.first);
    return names;
}

} // namespace hci