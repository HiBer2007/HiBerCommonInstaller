#pragma once

#include <map>
#include <string>

namespace hci {

// Variable store with {name} template interpolation.
// Unknown placeholders interpolate to empty strings.
class Vars {
public:
    void set(const std::string& name, const std::string& value);
    void setBool(const std::string& name, bool value);
    void setInt(const std::string& name, long long value);

    std::string get(const std::string& name) const;
    bool has(const std::string& name) const;
    bool getBool(const std::string& name, bool defaultValue = false) const;

    void remove(const std::string& name);
    void clear();

    // Replace every "{name}" occurrence with its value (recursive, cyclic-safe).
    std::string interpolate(const std::string& text) const;

    // All variables as a flat map snapshot.
    const std::map<std::string, std::string>& all() const { return values_; }

private:
    std::string interpolateOnce(const std::string& text) const;

    std::map<std::string, std::string> values_;
};

} // namespace hci