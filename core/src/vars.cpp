#include "hci/vars.h"

#include <algorithm>
#include <cctype>

namespace hci {

void Vars::set(const std::string& name, const std::string& value)
{
    values_[name] = value;
}

void Vars::setBool(const std::string& name, bool value)
{
    values_[name] = value ? "true" : "false";
}

void Vars::setInt(const std::string& name, long long value)
{
    values_[name] = std::to_string(value);
}

std::string Vars::get(const std::string& name) const
{
    auto it = values_.find(name);
    return it == values_.end() ? std::string() : it->second;
}

bool Vars::has(const std::string& name) const
{
    return values_.find(name) != values_.end();
}

bool Vars::getBool(const std::string& name, bool defaultValue) const
{
    auto it = values_.find(name);
    if (it == values_.end()) return defaultValue;
    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return defaultValue;
}

void Vars::remove(const std::string& name)
{
    values_.erase(name);
}

void Vars::clear()
{
    values_.clear();
}

std::string Vars::interpolateOnce(const std::string& text) const
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            size_t close = text.find('}', i + 1);
            if (close != std::string::npos) {
                std::string name = text.substr(i + 1, close - i - 1);
                auto it = values_.find(name);
                if (it != values_.end()) {
                    out += it->second;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i];
        ++i;
    }
    return out;
}

std::string Vars::interpolate(const std::string& text) const
{
    // Recursive-safe: up to 8 expansion passes (cyclic references settle).
    std::string cur = text;
    for (int pass = 0; pass < 8; ++pass) {
        std::string next = interpolateOnce(cur);
        if (next == cur) break;
        cur = std::move(next);
    }
    return cur;
}

} // namespace hci