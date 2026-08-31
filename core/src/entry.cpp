#include "hci/entry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <vector>

namespace hci {
namespace entry {

std::string EntryOptions::resolveMode(const ProductConfig& product) const
{
    if (!mode.empty()) return mode;
    std::string m = product.defaultMode;
    if (m.empty()) m = "gui";
    return m;
}

// ------------------------------------------------------------------
// ASCII banner fonts: 4-row compact glyph table (variable width).
// v1 ships a single built-in font; "slant" and "standard" both map to it.
// ------------------------------------------------------------------
namespace {

using Glyph = std::array<std::string, 4>;

const std::map<char, Glyph>& glyphTable()
{
    static const std::map<char, Glyph> table = {
        {' ', {"    ", "    ", "    ", "    "}},
        {'A', {" /\\ ", "/  \\", "|  |", "|__|"}},
        {'B', {" __ ", "| _)", "| _>", "|__)"}},
        {'C', {" __ ", "/ _|", "| |_", "\\__|"}},
        {'D', {" __ ", "| _\\", "| |)", "|__/"}},
        {'E', {" __ ", "|_  ", "|_| ", "|__ "}},
        {'F', {" __ ", "|_  ", "| | ", "| | "}},
        {'G', {" __ ", "/ _|", "| (_", "\\__|"}},
        {'H', {"| | ", "|_| ", "| | ", "|_| "}},
        {'I', {" _ ", "| |", "| |", "|_|"}},
        {'J', {"  _", "  |", "  |", "|_|"}},
        {'K', {"| | ", "|/  ", "|\\  ", "|_| "}},
        {'L', {"|   ", "|   ", "|   ", "|__ "}},
        {'M', {"/\\/\\", "|\\/|", "|  |", "|  |"}},
        {'N', {"|\\  ", "| \\ ", "| | ", "|_| "}},
        {'O', {" __ ", "/  \\", "|  |", "\\__/"}},
        {'P', {" __ ", "| _)", "| _>", "|   "}},
        {'Q', {" __ ", "/  \\", "|  |", "\\_\\/"}},
        {'R', {" __ ", "| _)", "| _>", "|__)"}},
        {'S', {" __ ", "/ _)", "\\ _>", "\\__/"}},
        {'T', {"___ ", " | |", " | |", " |_|"}},
        {'U', {"| | ", "| | ", "| | ", "\\_/ "}},
        {'V', {"\\   /", " \\ / ", "  \\  ", "  |  "}},
        {'W', {"\\   /\\   /", " \\ /  \\ / ", "  V    V  ", "  |    |  "}},
        {'X', {"\\   /", " \\ / ", " / \\ ", "/   \\"}},
        {'Y', {"\\   /", " \\ / ", "  |  ", "  |  "}},
        {'Z', {"____", "   /", "  / ", " /_ "}},
        {'0', {" __ ", "/  \\", "|  |", "\\__/"}},
        {'1', {" _ ", "/ |", "| |", "|_|"}},
        {'2', {" ___", "|_  )", " _ /", "|_  "}},
        {'3', {" ___", "|_  )", " _  )", "|__/"}},
        {'4', {"| | ", "|_| ", "  | ", "  | "}},
        {'5', {" __ ", "| _ ", "|__/", "\\__/"}},
        {'6', {" __ ", "/ _|", "| (_", "\\__|"}},
        {'7', {"___ ", "  / ", " /  ", "/   "}},
        {'8', {" __ ", "/ _\\", "\\_ /", "\\__/"}},
        {'9', {" __ ", "/ _\\", "\\__/", "   /"}},
        {'.', {"    ", "    ", " __ ", "|__|"}},
        {'-', {"    ", " ___", "    ", "    "}},
        {'_', {"    ", "    ", "    ", "____"}},
        {':', {"    ", " __ ", " __ ", "    "}},
        {'/', {"   /", "  / ", " /  ", "/   "}},
        {'\\', {"\\   ", " \\  ", "  \\ ", "   \\"}},
        {'(', {" / ", "|  ", "|  ", " / "}},
        {')', {" \\ ", "  |", "  |", " \\ "}},
        {'!', {" | ", " | ", " | ", " | "}},
        {'?', {" __ ", "/ _)", "  / ", " |  "}},
    };
    return table;
}

} // namespace

std::string renderBanner(const std::string& productName, const std::string& /*font*/)
{
    const auto& table = glyphTable();

    std::string upper = productName;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::vector<const Glyph*> glyphs;
    size_t width = 0;
    for (char c : upper) {
        auto it = table.find(c);
        if (it == table.end()) continue;
        glyphs.push_back(&it->second);
        width += it->second[0].size() + 1;
    }

    std::string out;
    if (glyphs.empty() || width > 100) {
        // Fallback: plain uppercase line (banner too long or unknown text).
        out = "  " + upper + "\n";
    } else {
        for (int row = 0; row < 4; ++row) {
            out += "  ";
            for (size_t i = 0; i < glyphs.size(); ++i) {
                out += (*glyphs[i])[static_cast<size_t>(row)];
                if (i + 1 < glyphs.size()) out += ' ';
            }
            out += "\n";
        }
    }
    out += "\n";
    out += "  Powered by HiBer Common Installer Module\n";
    return out;
}

} // namespace entry
} // namespace hci