#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace hci {

// ------------------------------------------------------------------
// Event bus: topic publish/subscribe (sync publish; queued async posts).
// ------------------------------------------------------------------
using EventHandler = std::function<void(const nlohmann::json& payload)>;
using HandlerId = unsigned long long;

class EventBus {
public:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Subscribe to a topic; returns a handler id usable with unsubscribe().
    HandlerId subscribe(const std::string& topic, EventHandler handler);

    void unsubscribe(HandlerId id);
    void unsubscribeAll(const std::string& topic);

    // Synchronous publish: invokes all handlers of the topic in registration order.
    void publish(const std::string& topic, const nlohmann::json& payload = nullptr);

    // Queued post: stores the event; delivered on the next drain() call.
    void post(const std::string& topic, const nlohmann::json& payload = nullptr);
    void drain();

private:
    struct Entry {
        HandlerId id = 0;
        std::string topic;
        EventHandler handler;
    };
    struct Pending {
        std::string topic;
        nlohmann::json payload;
    };

    std::vector<Entry> entries_;
    std::vector<Pending> pending_;
    HandlerId nextId_ = 1;
    bool publishing_ = false;
};

// ------------------------------------------------------------------
// Service registry: type-erased service lookup for extension-to-extension
// and host-to-extension calls.
// ------------------------------------------------------------------
class ServiceRegistry {
public:
    void registerService(const std::string& ifaceName, void* service);
    void unregisterService(const std::string& ifaceName);
    void* findService(const std::string& ifaceName) const;

    template <typename T>
    void registerService(T* service)
    {
        registerService(std::string(typeid(T).name()), static_cast<void*>(service));
    }

    template <typename T>
    T* service() const
    {
        return static_cast<T*>(findService(std::string(typeid(T).name())));
    }

    std::vector<std::string> serviceNames() const;

private:
    std::map<std::string, void*> services_;
};

} // namespace hci