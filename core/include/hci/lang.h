// hci::lang - shell chrome i18n (shared by GUI/TUI; CLI text stays English
// for scripting). Product-provided texts are NOT translated here.
// Source stays plain ASCII: non-ASCII strings use \uXXXX escapes.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace hci {
namespace lang {

// Translate a shell text by code ("en" default / "zh"); unknown keys fall
// back to English. Product texts are untouched by design.
std::string tr(const std::string& code, const std::string& en);

// Human-readable names for the picker: {"en","English"}, {"zh","\u7b80..."}.
std::vector<std::pair<std::string, std::string>> availableLanguages();

} // namespace lang
} // namespace hci