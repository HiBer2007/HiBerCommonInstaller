// hci_gui i18n: shell chrome texts (buttons, labels, dialogs).
// Product-provided texts (productName, prompts, messages) are NOT translated
// here - the product config is responsible for those.
// Source stays plain ASCII: non-ASCII strings use \uXXXX escapes.

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hci {
namespace gui {

// Translate a shell text by language code ("en" default / "zh"). Unknown
// keys fall back to English. Product texts are untouched by design.
std::string tr(const std::string& lang, const std::string& en);

// Human-readable language names for the picker.
std::vector<std::pair<std::string, std::string>> availableLanguages();
// {"en", "English"}, {"zh", "\u7b80\u4f53\u4e2d\u6587"}

} // namespace gui
} // namespace hci