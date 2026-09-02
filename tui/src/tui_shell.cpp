#include "tui_shell.h"

#include "hci/entry.h"
#include "hci/extension.h"
#include "hci/log.h"
#include "hci/port.h"
#include "hci/script.h"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace hci {
namespace tui {

namespace {
std::string dirOf(const std::string& p)
{
    size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}
} // namespace

// ------------------------------------------------------------------
// WCWIDTH (compact East Asian Width ranges; combining = 0)
// ------------------------------------------------------------------
namespace {

bool inRange(unsigned cp, unsigned lo, unsigned hi) { return cp >= lo && cp <= hi; }

bool isWide(unsigned cp)
{
    // CJK unified / ext A / compatibility / radicals / kana / hangul / fullwidth
    if (inRange(cp, 0x1100, 0x115F)) return true;
    if (inRange(cp, 0x2E80, 0x2EFF)) return true;
    if (inRange(cp, 0x3000, 0x303F)) return true;
    if (inRange(cp, 0x3040, 0x30FF)) return true;
    if (inRange(cp, 0x3105, 0x312F)) return true;
    if (inRange(cp, 0x3131, 0x318E)) return true;
    if (inRange(cp, 0x3190, 0x3247)) return true;
    if (inRange(cp, 0x3250, 0x4DBF)) return true; // CJK ext A
    if (inRange(cp, 0x4E00, 0x9FFF)) return true; // CJK unified
    if (inRange(cp, 0xA000, 0xA4CF)) return true; // Yi
    if (inRange(cp, 0xAC00, 0xD7A3)) return true; // Hangul syllables
    if (inRange(cp, 0xF900, 0xFAFF)) return true; // CJK compat
    if (inRange(cp, 0xFE10, 0xFE19)) return true;
    if (inRange(cp, 0xFE30, 0xFE6F)) return true; // CJK compat forms
    if (inRange(cp, 0xFF00, 0xFF60)) return true; // fullwidth forms
    if (inRange(cp, 0xFFE0, 0xFFE6)) return true;
    if (inRange(cp, 0x20000, 0x3FFFD)) return true; // CJK ext B+
    // Common emoji blocks
    if (inRange(cp, 0x1F004, 0x1F004)) return true;
    if (inRange(cp, 0x1F0CF, 0x1F0CF)) return true;
    if (inRange(cp, 0x1F18E, 0x1F1FF)) return true;
    if (inRange(cp, 0x1F300, 0x1F64F)) return true;
    if (inRange(cp, 0x1F680, 0x1F6FF)) return true;
    if (inRange(cp, 0x1F900, 0x1F9FF)) return true;
    if (inRange(cp, 0x2600, 0x27BF)) return true;
    if (inRange(cp, 0x2B00, 0x2BFF)) return true;
    return false;
}

bool isZeroWidth(unsigned cp)
{
    if (inRange(cp, 0x0300, 0x036F)) return true;  // combining diacritics
    if (inRange(cp, 0x1AB0, 0x1AFF)) return true;
    if (inRange(cp, 0x1DC0, 0x1DFF)) return true;
    if (inRange(cp, 0x20D0, 0x20FF)) return true;
    if (inRange(cp, 0xFE00, 0xFE0F)) return true;  // variation selectors
    if (inRange(cp, 0xFE20, 0xFE2F)) return true;
    if (inRange(cp, 0x200C, 0x200F)) return true;  // zero-width joiners
    return false;
}

struct Decoded {
    unsigned cp = 0;
    int len = 0;
};

Decoded decodeUtf8(const char* s)
{
    Decoded d;
    unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) { d.cp = c; d.len = 1; return d; }
    if ((c & 0xE0) == 0xC0 && (static_cast<unsigned char>(s[1]) & 0xC0) == 0x80) {
        d.cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[1]) & 0x3F);
        d.len = 2;
    } else if ((c & 0xF0) == 0xE0 && (static_cast<unsigned char>(s[1]) & 0xC0) == 0x80
               && (static_cast<unsigned char>(s[2]) & 0xC0) == 0x80) {
        d.cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[1]) & 0x3F) << 6)
             | (static_cast<unsigned char>(s[2]) & 0x3F);
        d.len = 3;
    } else if ((c & 0xF8) == 0xF0 && (static_cast<unsigned char>(s[1]) & 0xC0) == 0x80
               && (static_cast<unsigned char>(s[2]) & 0xC0) == 0x80
               && (static_cast<unsigned char>(s[3]) & 0xC0) == 0x80) {
        d.cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[1]) & 0x3F) << 12)
             | ((static_cast<unsigned char>(s[2]) & 0x3F) << 6)
             | (static_cast<unsigned char>(s[3]) & 0x3F);
        d.len = 4;
    } else {
        d.cp = c;
        d.len = 1;
    }
    return d;
}

} // namespace

size_t displayWidth(const std::string& utf8)
{
    size_t w = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        Decoded d = decodeUtf8(utf8.data() + i);
        if (isZeroWidth(d.cp)) {
            // zero
        } else if (isWide(d.cp)) {
            w += 2;
        } else {
            w += 1;
        }
        i += static_cast<size_t>(d.len);
    }
    return w;
}

std::vector<std::string> wrapText(const std::string& text, size_t width)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty()) { out.emplace_back(); continue; }

        std::string cur;
        size_t curW = 0;
        size_t i = 0;
        while (i < line.size()) {
            Decoded d = decodeUtf8(line.data() + i);
            size_t cw = isZeroWidth(d.cp) ? 0 : (isWide(d.cp) ? 2 : 1);
            if (curW + cw > width && !cur.empty()) {
                out.push_back(cur);
                cur.clear();
                curW = 0;
            }
            cur.append(line, i, static_cast<size_t>(d.len));
            curW += cw;
            i += static_cast<size_t>(d.len);
        }
        out.push_back(cur);
    }
    return out;
}

std::string ansi(const char* code, const std::string& text)
{
    return std::string("\x1b[") + code + "m" + text + "\x1b[0m";
}

// ------------------------------------------------------------------
TuiShell::TuiShell(const hci::ProductConfig& product, const std::string& flowFile,
                   const std::string& installPath, const std::string& language)
    : product_(product), flowFile_(flowFile)
{
    if (!installPath.empty()) ctx_.vars().set("installDir", installPath);
    if (!language.empty()) {
        lang_ = language;
        ctx_.vars().set("language", language);
    } else {
        lang_ = product.defaultLanguage.empty() ? "en" : product.defaultLanguage;
    }
    autopilot_ = !port::getEnv("HCI_TUI_AUTOPILOT").empty();
    std::string cols = port::getEnv("COLUMNS");
    if (!cols.empty()) {
        try { width_ = static_cast<size_t>(std::stoi(cols)); } catch (...) {}
    }
    if (width_ < 40) width_ = 80;
}

void TuiShell::clear()
{
    std::cout << "\x1b[2J\x1b[H";
    std::cout.flush();
}

void TuiShell::print(const std::string& line)
{
    std::cout << line << "\n";
}

void TuiShell::printTitle(const std::string& line)
{
    const size_t w = width();
    std::string sep;
    size_t lw = displayWidth(line);
    size_t pad = lw >= w ? 0 : (w - lw) / 2;
    sep.assign(pad > 0 ? pad - 1 : 0, ' ');
    std::cout << ansi("1", sep + line) << "\n";
    std::cout << ansi("2", std::string(w > 4 ? w - 2 : w, '-')) << "\n";
    std::cout.flush();
}

void TuiShell::printError(const std::string& line)
{
    std::cout << ansi("31", "[ERROR] " + line) << "\n";
    std::cout.flush();
}

void TuiShell::printProgress(const std::string& label, int percent)
{
    size_t w = width();
    size_t barW = w > 24 ? w - 24 : 24;
    int pct = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    size_t fill = (percent < 0) ? barW : static_cast<size_t>(pct * static_cast<int>(barW) / 100);
    // UTF-8 bytes for U+2588 (full block) and U+2591 (light shade).
    static const std::string kFull = "\xE2\x96\x88";
    static const std::string kShade = "\xE2\x96\x91";
    std::string bar;
    bar.reserve(fill * 3 + (barW - fill) * 3);
    for (size_t i = 0; i < fill; ++i) bar += kFull;
    for (size_t i = fill; i < barW; ++i) bar += kShade;
    std::string pctStr = percent < 0 ? "??" : std::to_string(pct) + "%";
    std::cout << "\r" << ansi("36", "[" + bar + "]") << " "
              << ansi("1", pctStr) << " " << label;
    std::cout.flush();
}

std::string TuiShell::prompt(const std::string& question, const std::string& def)
{
    std::cout << question;
    if (!def.empty()) std::cout << " [" << def << "]";
    std::cout << ": " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    if (std::cin.eof()) return def;
    return line.empty() ? def : line;
}

bool TuiShell::confirm(const std::string& question, bool defaultYes)
{
    std::cout << question << (defaultYes ? " [Y/n] " : " [y/N] ") << std::flush;
    std::string line;
    std::getline(std::cin, line);
    if (std::cin.eof()) return defaultYes;
    if (line.empty()) return defaultYes;
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));
    return c == 'y';
}

int TuiShell::select(const std::string& q, const std::vector<std::string>& choices,
                     int defaultIndex)
{
    print(q);
    for (size_t i = 0; i < choices.size(); ++i)
        print("  [" + std::to_string(i) + "] " + choices[i]);
    std::string line = prompt("> ", std::to_string(defaultIndex));
    try {
        int v = std::stoi(line);
        if (v >= 0 && v < static_cast<int>(choices.size())) return v;
    } catch (...) {}
    return defaultIndex;
}

bool TuiShell::toggleList(const std::string& title,
                          const std::vector<std::string>& labels,
                          std::vector<bool>& checked)
{
    while (true) {
        printTitle(title);
        for (size_t i = 0; i < labels.size(); ++i) {
            std::string mark = checked[i] ? "[x]" : "[ ]";
            print("  " + ansi(checked[i] ? "32" : "37", mark) + " " + labels[i]);
        }
        print("  toggle id, blank = done");
        std::string line = prompt("> ", "");
        if (line.empty() || std::cin.eof()) return true;
        try {
            int v = std::stoi(line);
            if (v >= 0 && v < static_cast<int>(checked.size())) checked[static_cast<size_t>(v)] = !checked[static_cast<size_t>(v)];
        } catch (...) {}
    }
}

int TuiShell::run()
{
    hci::FlowSpec flow;
    try {
        flow = hci::FlowSpec::loadFile(flowFile_);
    } catch (const std::exception& e) {
        printError(e.what());
        return 1;
    }

    auto script = hci::createLuaEngine();
    hci::EventBus bus;
    hci::ServiceRegistry services;
    hci::ExtensionRegistry registry;
    hci::ExtensionLoader loader(&bus, &services, &ctx_, &product_, &registry);
    loader.loadStatic();
    std::string extDir = port::joinPath(port::exeDir(), "extensions");
    if (fs::exists(fs::u8path(extDir))) loader.loadDirectory(extDir);

    hci::tui::TuiFlowUi ui(*this);
    hci::FlowRunner runner(product_, ctx_, &ui, script.get());
    runner.setEventBus(&bus);
    runner.setRegistry(&registry);
    runner.setBaseDir(dirOf(flowFile_));

    int rc = runner.run(flow);
    loader.shutdownAll();
    return rc;
}

// ------------------------------------------------------------------
TuiFlowUi::TuiFlowUi(TuiShell& shell) : shell_(shell)
{
    autopilot_ = shell.autopilot();
}

bool TuiFlowUi::onWelcome(const std::string& productName,
                          const hci::ProductConfig& product)
{
    shell_.clear();
    shell_.print(entry::renderBanner(productName, product.bannerFont));
    shell_.printTitle(productName + " - setup");
    if (autopilot_) return true;
    shell_.prompt(shell_.tr("Press Enter to continue"));
    return true;
}

bool TuiFlowUi::onLicense(const std::string& text, bool& accepted)
{
    if (autopilot_) { accepted = true; return true; }
    shell_.clear();
    shell_.printTitle(shell_.tr("License"));
    // Truncate long license text to the first 40 lines.
    std::vector<std::string> lines;
    {
        std::istringstream ss(text);
        std::string l;
        while (std::getline(ss, l) && lines.size() < 40) lines.push_back(l);
    }
    for (auto& l : lines) {
        for (auto& wl : wrapText(l, shell_.width())) shell_.print(wl);
    }
    if (text.size() > 0 && lines.size() == 40) shell_.print("[... truncated ...]");
    accepted = shell_.confirm("\n" + shell_.tr("Accept the license?"));
    return true;
}

bool TuiFlowUi::onPath(std::string& path, const std::string& defaultPath)
{
    if (autopilot_) { if (path.empty()) path = defaultPath; return true; }
    std::string value = shell_.prompt(shell_.tr("Install directory"), path.empty() ? defaultPath : path);
    path = value;
    return true;
}

bool TuiFlowUi::onComponents(const std::vector<hci::ProductComponent>& components,
                             std::vector<bool>& checked)
{
    if (autopilot_) return true;
    std::vector<std::string> labels;
    for (auto& c : components) labels.push_back(c.label);
    return shell_.toggleList("Components", labels, checked);
}

bool TuiFlowUi::onOption(const std::string& prompt,
                         const std::vector<std::string>& choices, int& selected)
{
    if (autopilot_) return true;
    selected = shell_.select(prompt, choices, selected);
    return true;
}

bool TuiFlowUi::onConfirm(const std::string& prompt, bool& yes)
{
    if (autopilot_) return true;
    yes = shell_.confirm(prompt, yes);
    return true;
}

bool TuiFlowUi::onInput(const std::string& prompt, std::string& value, bool required)
{
    if (autopilot_) return !required;
    value = shell_.prompt(prompt, "");
    if (required && value.empty()) {
        shell_.printError(shell_.tr("A value is required"));
        return onInput(prompt, value, required);
    }
    return true;
}

void TuiFlowUi::onProgress(const std::string& step, int percent, const std::string& detail)
{
    std::string label = step;
    if (!detail.empty()) label += " - " + detail;
    shell_.printProgress(label, percent);
}

void TuiFlowUi::onMessage(const std::string& text, bool isError)
{
    if (isError) shell_.printError(text);
    else shell_.print(text);
}

void TuiFlowUi::onFinish(bool success, const std::string& message,
                         const std::string& launchExe)
{
    shell_.print("");
    if (success) {
        shell_.print(ansi("32;1", "[OK] " + message));
        if (!launchExe.empty()) shell_.print("Launch: " + launchExe);
    } else {
        shell_.print(ansi("31;1", "[FAIL] " + message));
    }
    if (autopilot_) return;
    shell_.prompt("\nPress Enter to exit");
}

} // namespace tui
} // namespace hci