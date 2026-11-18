#include "acgc_dialog_messages.hpp"
#include "pc_dialog.h"

#include "acgc/game_text.hpp"
#include "borealis/log.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

extern "C" int mFont_GetCodeWidth(uint8_t c, int cut);

namespace acgc::dialog::msg {
namespace {

constexpr borealis::Log Log{ "acgc::dialog" };

// mFont_cont_info_tbl sizes (m_font_main.c_inc), indexed by mFont_CONT_CODE_*.
constexpr std::array<uint8_t, 123> kCodeSizes = {
    2, 2, 2, 3, 2, 5, 2, 2, 5, 5, 5, 5, 5, 2, 4, 4, 4, 4, 4, 6, 8, 10, 6, 8, 10, 2, // 0-25
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,                     // 26-45
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,                        // 46-64
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,                                    // 65-79
    6, 3, 3, 3, 3, 2, 4, 4, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 6,                     // 80-99
    3, 3, 4, 3, 2, 2, 6, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 4, 4, 12, 14,          // 100-122
};

constexpr std::array<const char*, 123> kCodeNames = {
    "LAST", "CONTINUE", "CLEAR", "CURSOR_SET_TIME", "BUTTON", "COLOR", "ABLE_CANCEL", "UNABLE_CANCEL",
    "DEMO_ORDER_PLAYER", "DEMO_ORDER_NPC0", "DEMO_ORDER_NPC1", "DEMO_ORDER_NPC2", "DEMO_ORDER_QUEST",
    "SELECT_WINDOW", "NEXT_F", "NEXT_0", "NEXT_1", "NEXT_2", "NEXT_3", "NEXT_RANDOM_2", "NEXT_RANDOM_3",
    "NEXT_RANDOM_4", "SELECT_STRING_2", "SELECT_STRING_3", "SELECT_STRING_4", "FORCE_NEXT", "PLAYER_NAME",
    "TALK_NAME", "TAIL", "YEAR", "MONTH", "WEEK", "DAY", "HOUR", "MIN", "SEC", "FREE0", "FREE1", "FREE2",
    "FREE3", "FREE4", "FREE5", "FREE6", "FREE7", "FREE8", "FREE9", "DETERMINATION", "COUNTRY_NAME",
    "RANDOM_NUMBER_2", "ITEM0", "ITEM1", "ITEM2", "ITEM3", "ITEM4", "FREE10", "FREE11", "FREE12", "FREE13",
    "FREE14", "FREE15", "FREE16", "FREE17", "FREE18", "FREE19", "MAIL", "DESTINY0", "DESTINY1", "DESTINY2",
    "DESTINY3", "DESTINY4", "DESTINY5", "DESTINY6", "DESTINY7", "DESTINY8", "DESTINY9", "FACE_NORMAL",
    "FACE_ANGRY", "FACE_SAD", "FACE_FUN", "FACE_SLEEPY", "COLOR_CHAR", "SOUND_CUT", "LINE_OFFSET",
    "LINE_TYPE", "CHAR_SCALE", "BUTTON2", "BGM_MAKE", "BGM_DELETE", "MSG_TIME_END", "SOUND_TRG_SYS",
    "LINE_SCALE", "SOUND_NO_PAGE", "VOICE_TRUE", "VOICE_FALSE", "SELECT_NO_B", "GIVE_OPEN", "GIVE_CLOSE",
    "FACE_GLOOMY", "SELECT_NO_B_CLOSE", "NEXT_RANDOM_SECTION", "AGB_DUMMY0", "AGB_DUMMY1", "AGB_DUMMY2",
    "SPACE", "AGB_DUMMY3", "AGB_DUMMY4", "AGB_MALE_FEMALE_CHECK", "AGB_DUMMY5", "AGB_DUMMY6", "AGB_DUMMY7",
    "AGB_DUMMY8", "AGB_DUMMY9", "AGB_DUMMY10", "ISLAND_NAME", "SET_CURSOR_JUST", "CLR_CURSOR_JUST",
    "CUT_ARTICLE", "CAPITAL_LETTER", "AM_PM", "NEXT_4", "NEXT_5", "SELECT_STRING_5", "SELECT_STRING_6",
};

constexpr uint8_t kCharNewLine = 205;

struct Archive {
    std::vector<uint32_t> ends; // end offset of each message
    std::vector<uint8_t> data;
};

Archive g_archive;
std::atomic<bool> g_ready{ false };

bool is_end_code(const uint8_t* p, size_t remaining) {
    return remaining >= 2 && p[0] == kCharControl && (p[1] == 0x00 || p[1] == 0x01 || p[1] == 88);
}

} // namespace

int code_size(const uint8_t* p, size_t remaining) {
    int size = 1;
    if (p[0] == kCharControl) {
        size = remaining >= 2 && p[1] < kCodeSizes.size() ? kCodeSizes[p[1]] : 2;
    } else if (p[0] == kCharMessageTag) {
        size = 2;
    }
    return std::max(1, std::min<int>(size, static_cast<int>(remaining)));
}

const char* code_name(uint8_t code) { return code < kCodeNames.size() ? kCodeNames[code] : "UNKNOWN"; }

void set_archive(const uint8_t* table, uint32_t tableSize, const uint8_t* data, uint32_t dataSize) {
    if (g_ready.load() || table == nullptr || data == nullptr) {
        return;
    }
    Archive a;
    a.ends.resize(tableSize / 4);
    for (size_t i = 0; i < a.ends.size(); ++i) {
        const uint8_t* p = table + i * 4;
        a.ends[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    a.data.assign(data, data + dataSize);
    g_archive = std::move(a);
    g_ready.store(true);
    Log.info("Message archive: {} messages, {} bytes", g_archive.ends.size(), g_archive.data.size());

    if (const char* dump = std::getenv("NOOKERY_DUMP_MESSAGES"); dump != nullptr && *dump != '\0') {
        dump_all(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(dump))));
    }
}

bool available() { return g_ready.load(); }

int count() { return available() ? static_cast<int>(g_archive.ends.size()) : 0; }

std::optional<std::vector<uint8_t>> original(int index) {
    if (!available() || index < 0 || index >= count()) {
        return std::nullopt;
    }
    const uint32_t start = index == 0 ? 0 : g_archive.ends[index - 1];
    const uint32_t end = g_archive.ends[index];
    if (end < start || end > g_archive.data.size()) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes(g_archive.data.begin() + start, g_archive.data.begin() + end);
    for (size_t i = 0; i < bytes.size();) {
        const size_t remaining = bytes.size() - i;
        const int size = code_size(&bytes[i], remaining);
        if (is_end_code(&bytes[i], remaining)) {
            bytes.resize(i + size);
            break;
        }
        i += size;
    }
    return bytes;
}

std::string describe(const std::vector<uint8_t>& bytes) {
    std::string out;
    for (size_t i = 0; i < bytes.size();) {
        const size_t remaining = bytes.size() - i;
        const int size = code_size(&bytes[i], remaining);
        const uint8_t c = bytes[i];
        if (c == kCharControl && remaining >= 2) {
            out += '<';
            out += code_name(bytes[i + 1]);
            for (int a = 2; a + 1 < size; a += 2) {
                out += ' ';
                out += std::to_string((int(bytes[i + a]) << 8) | bytes[i + a + 1]);
            }
            if (size > 2 && size % 2 == 1) {
                char hex[8];
                std::snprintf(hex, sizeof(hex), " x%02X", bytes[i + size - 1]);
                out += hex;
            }
            out += '>';
        } else if (c == kCharMessageTag) {
            out += "<TAG>";
        } else if (c == kCharNewLine) {
            out += "\\n";
        } else {
            out += acgc::text::encode(c);
        }
        i += size;
    }
    return out;
}

void dump_all(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        Log.warn("Could not write message dump to {}", path.string());
        return;
    }
    // Raw bytes too (u32 index, u32 length, bytes; little-endian), for offline tests.
    auto binPath = path;
    binPath += ".bin";
    std::ofstream bin(binPath, std::ios::binary);
    for (int i = 0; i < count(); ++i) {
        if (auto m = original(i); m && !m->empty()) {
            out << i << '\t' << describe(*m) << '\n';
            const uint32_t header[2] = { static_cast<uint32_t>(i), static_cast<uint32_t>(m->size()) };
            bin.write(reinterpret_cast<const char*>(header), sizeof(header));
            bin.write(reinterpret_cast<const char*>(m->data()), static_cast<std::streamsize>(m->size()));
        }
    }
    Log.info("Wrote message dump to {}", path.string());
}

// ---------------------------------------------------------------------------
// Rewrite templates

namespace {

// mFont_CONT_CODE_* values used below.
constexpr uint8_t kLast = 0, kContinue = 1, kClear = 2, kCursorSetTime = 3, kButton = 4;
constexpr uint8_t kDemoPlayer = 8, kDemoNpc0 = 9, kDemoNpc2 = 11, kDemoQuest = 12;
constexpr uint8_t kPlayerName = 26, kTalkName = 27, kTail = 28;
constexpr uint8_t kFree0 = 36, kItem0 = 49, kFree10 = 54, kMail = 64;
constexpr uint8_t kMsgTimeEnd = 88, kSpace = 103, kIslandName = 113, kCutArticle = 116, kAmPm = 118;
constexpr uint8_t kCharSpace = 0x20;
constexpr int kLineWidth = 190;  // text area is 192 px wide (m_msg_draw_font.c_inc)
constexpr int kLinesPerPage = 4; // mMsg_MAX_LINE

bool is_placeholder_code(uint8_t c) { return (c >= kPlayerName && c <= kMail) || c == kIslandName || c == kAmPm; }
bool is_item(uint8_t c) { return c >= kItem0 && c < kItem0 + 5; }

enum class Kind { Drop, Space, Keep, Unsafe };

Kind classify(uint8_t c) {
    if (c == kCursorSetTime || c == kButton) {
        // Typing pauses and "press A" waits inside a page: they're timed to the
        // original words, and the rewrite is laid out again from scratch.
        return Kind::Drop;
    }
    if (c == kSpace) {
        return Kind::Space;
    }
    if ((c >= kDemoNpc0 && c <= kDemoNpc2) || (c >= 75 && c <= 79) || c == 97 || c == kDemoPlayer ||
        c == kDemoQuest || c == 6 || c == 7 || (c >= 65 && c <= 74) || c == 86 || c == 87 || c == 89 || c == 92 ||
        c == 93 || c == 95 || c == 96) {
        // Animations, expressions, quest steps, music, sounds, cancel rules: kept in
        // order, before the new words if they came before the old ones, else after.
        return Kind::Keep;
    }
    return Kind::Unsafe; // colors, text effects, branches: leave the page alone
}

const std::map<std::string, uint8_t>& simple_keys() {
    static const std::map<std::string, uint8_t> keys = {
        { "player", kPlayerName }, { "name", kTalkName }, { "catchphrase", kTail }, { "year", 29 },
        { "month", 30 },           { "weekday", 31 },     { "day", 32 },           { "hour", 33 },
        { "minute", 34 },          { "second", 35 },      { "determination", 46 }, { "town", 47 },
        { "number", 48 },          { "mail", kMail },     { "island", kIslandName }, { "am_pm", kAmPm },
    };
    return keys;
}

std::string placeholder_key(uint8_t c, bool cut) {
    if (is_item(c)) {
        return (cut ? "item" : "a_item") + std::to_string(c - kItem0);
    }
    if (c >= kFree0 && c < kFree0 + 10) {
        return "free" + std::to_string(c - kFree0);
    }
    if (c >= kFree10 && c < kFree10 + 10) {
        return "free" + std::to_string(10 + c - kFree10);
    }
    for (const auto& [key, code] : simple_keys()) {
        if (code == c) {
            return key;
        }
    }
    return {};
}

// "{a_item3}" -> ITEM3 without cut; "{item3}" -> ITEM3 with cut; "{free15}" -> FREE15.
bool parse_key(const std::string& key, uint8_t& code, bool& cut) {
    cut = false;
    auto number = [&](size_t from, int max) -> int {
        if (from >= key.size() || key.size() - from > 2) {
            return -1;
        }
        int n = 0;
        for (size_t i = from; i < key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
                return -1;
            }
            n = n * 10 + (key[i] - '0');
        }
        return n < max ? n : -1;
    };
    if (key.rfind("a_item", 0) == 0) {
        const int n = number(6, 5);
        code = static_cast<uint8_t>(kItem0 + n);
        return n >= 0;
    }
    if (key.rfind("item", 0) == 0) {
        const int n = number(4, 5);
        code = static_cast<uint8_t>(kItem0 + n);
        cut = true;
        return n >= 0;
    }
    if (key.rfind("free", 0) == 0) {
        const int n = number(4, 20);
        code = static_cast<uint8_t>(n < 10 ? kFree0 + n : kFree10 + n - 10);
        return n >= 0;
    }
    if (auto it = simple_keys().find(key); it != simple_keys().end()) {
        code = it->second;
        return true;
    }
    return false;
}

std::string collapse(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            space = !out.empty();
        } else {
            if (space) {
                out += ' ';
            }
            space = false;
            out += c;
        }
    }
    return out;
}

// Calls fn(key) for each {placeholder}; returns false if a brace is unbalanced.
template <typename Fn>
bool for_each_placeholder(const std::string& s, Fn fn) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '}') {
            return false;
        }
        if (s[i] == '{') {
            const size_t close = s.find('}', i);
            if (close == std::string::npos || s.find('{', i + 1) < close) {
                return false;
            }
            fn(s.substr(i + 1, close - i - 1));
            i = close;
        }
    }
    return true;
}

std::string without_placeholders(const std::string& s) {
    std::string out;
    int depth = 0;
    for (char c : s) {
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            depth = std::max(0, depth - 1);
        } else if (depth == 0) {
            out += c;
        }
    }
    return out;
}

std::vector<std::string> numbers_in(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            cur += c;
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

Page parse_page(const std::vector<uint8_t>& b, size_t start, size_t end) {
    Page p;
    p.original.assign(b.begin() + static_cast<std::ptrdiff_t>(start), b.begin() + static_cast<std::ptrdiff_t>(end));
    bool unsafe = false;
    bool seenText = false;
    bool pendingCut = false;
    std::map<std::string, int> freeCut; // 1 = cut, 2 = bare; mixing both can't be expressed
    std::string text;
    for (size_t i = start; i < end;) {
        const int size = code_size(&b[i], end - i);
        const uint8_t c = b[i];
        if (c == kCharControl && size >= 2) {
            const uint8_t code = b[i + 1];
            if (code == kCutArticle) {
                pendingCut = true;
            } else if (is_placeholder_code(code)) {
                const std::string key = placeholder_key(code, pendingCut);
                if (key.empty()) {
                    unsafe = true;
                } else {
                    if (!is_item(code) && key.rfind("free", 0) == 0) {
                        int& state = freeCut[key];
                        const int now = pendingCut ? 1 : 2;
                        if (state != 0 && state != now) {
                            unsafe = true;
                        }
                        state = now;
                    }
                    text += "{" + key + "}";
                    seenText = true;
                }
                pendingCut = false;
            } else {
                switch (classify(code)) {
                case Kind::Drop: break;
                case Kind::Space: text += ' '; break;
                case Kind::Keep: {
                    auto& dst = seenText ? p.trail : p.lead;
                    dst.insert(dst.end(), b.begin() + i, b.begin() + i + size);
                    break;
                }
                case Kind::Unsafe: unsafe = true; break;
                }
            }
        } else if (c == kCharMessageTag) {
            unsafe = true;
        } else if (c == kCharNewLine) {
            text += ' ';
        } else {
            text += acgc::text::encode(c);
            if (c != kCharSpace) {
                seenText = true;
            }
        }
        i += size;
    }
    // Whether a {freeN} had CUT_ARTICLE is re-derived from `original` at build time.
    p.text = collapse(text);
    p.rewritable = !unsafe &&
                   std::any_of(p.text.begin(), p.text.end(), [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)); });
    return p;
}

// FREE strings shown with CUT_ARTICLE in the original page keep it in the rewrite.
bool page_cuts_free(const Page& page, uint8_t code) {
    const auto& b = page.original;
    for (size_t i = 0; i + 3 < b.size();) {
        if (b[i] == kCharControl && b[i + 1] == kCutArticle && b[i + 2] == kCharControl && b[i + 3] == code) {
            return true;
        }
        i += code_size(&b[i], b.size() - i);
    }
    return false;
}

int default_width(uint8_t code) {
    if (is_item(code)) {
        return 100;
    }
    if (code == kPlayerName || code == kTalkName || code == kTail) {
        return 64;
    }
    if ((code >= kFree0 && code < kFree0 + 10) || (code >= kFree10 && code < kFree10 + 10)) {
        return 80;
    }
    return 40;
}

} // namespace

bool resolve_page_placeholder(const std::string& key, std::vector<uint8_t>& bytes, int& width, void* user) {
    auto* ctx = static_cast<PageResolver*>(user);
    uint8_t code = 0;
    bool cut = false;
    if (!parse_key(key, code, cut)) {
        return false;
    }
    if (!is_item(code) && key.rfind("free", 0) == 0) {
        cut = page_cuts_free(*ctx->page, code);
    }
    if (cut) {
        bytes.insert(bytes.end(), { kCharControl, kCutArticle });
    }
    bytes.insert(bytes.end(), { kCharControl, code });
    width = ctx->width ? ctx->width(code, cut, ctx->user) : -1;
    if (width < 0) {
        width = default_width(code);
    }
    return true;
}

int Template::rewritable_pages() const {
    return static_cast<int>(std::count_if(pages.begin(), pages.end(), [](const Page& p) { return p.rewritable; }));
}

std::vector<uint8_t> quest_codes(const std::vector<uint8_t>& b) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < b.size();) {
        const int size = code_size(&b[i], b.size() - i);
        if (b[i] == kCharControl && size >= 2 && b[i + 1] == kDemoQuest) {
            out.insert(out.end(), b.begin() + static_cast<std::ptrdiff_t>(i), b.begin() + static_cast<std::ptrdiff_t>(i + size));
        }
        i += size;
    }
    return out;
}

std::vector<int> next_messages(const std::vector<uint8_t>& b) {
    std::vector<int> out;
    for (size_t i = 0; i < b.size();) {
        const int size = code_size(&b[i], b.size() - i);
        if (b[i] == kCharControl && size >= 4) {
            const uint8_t code = b[i + 1];
            const bool next = (code >= 14 && code <= 21) || code == 99 || code == 119 || code == 120;
            for (int a = 2; next && a + 1 < size; a += 2) {
                out.push_back((int(b[i + a]) << 8) | b[i + a + 1]);
            }
        }
        i += size;
    }
    return out;
}

uint64_t fingerprint(const std::vector<uint8_t>& bytes) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : bytes) {
        h = (h ^ c) * 1099511628211ull;
    }
    return h;
}

std::optional<Template> parse_template(const std::vector<uint8_t>& b) {
    // Everything after the last visible word (choices, branches, end code) is the tail.
    size_t contentEnd = 0;
    for (size_t i = 0; i < b.size();) {
        const int size = code_size(&b[i], b.size() - i);
        const uint8_t c = b[i];
        if (c == kCharControl && size >= 2) {
            const uint8_t code = b[i + 1];
            if (code == kLast || code == kContinue || code == kMsgTimeEnd) {
                break;
            }
            if (is_placeholder_code(code)) {
                contentEnd = i + size;
            }
        } else if (c != kCharMessageTag && c != kCharNewLine && c != kCharSpace) {
            contentEnd = i + size;
        }
        i += size;
    }
    if (contentEnd == 0) {
        return std::nullopt;
    }

    Template t;
    size_t tailStart = contentEnd;
    while (tailStart < b.size() && b[tailStart] == kCharNewLine) {
        ++tailStart;
    }
    t.tail.assign(b.begin() + static_cast<std::ptrdiff_t>(tailStart), b.end());

    size_t pageStart = 0;
    for (size_t i = 0; i < contentEnd;) {
        const int size = code_size(&b[i], contentEnd - i);
        if (b[i] == kCharControl && size >= 2 && b[i + 1] == kButton) {
            // BUTTON, then only new lines and non-text codes, then CLEAR: a page break.
            size_t j = i + size;
            bool clear = false;
            while (j < contentEnd) {
                if (b[j] == kCharNewLine) {
                    ++j;
                    continue;
                }
                if (b[j] == kCharControl && j + 1 < contentEnd) {
                    const uint8_t code = b[j + 1];
                    if (code == kClear) {
                        clear = true;
                        j += 2;
                        break;
                    }
                    if (is_placeholder_code(code) || code == kCutArticle || code == kButton) {
                        break;
                    }
                    j += code_size(&b[j], contentEnd - j);
                    continue;
                }
                break;
            }
            if (clear) {
                t.pages.push_back(parse_page(b, pageStart, i));
                t.separators.emplace_back(b.begin() + static_cast<std::ptrdiff_t>(i), b.begin() + static_cast<std::ptrdiff_t>(j));
                pageStart = j;
                i = j;
                continue;
            }
        }
        i += size;
    }
    t.pages.push_back(parse_page(b, pageStart, contentEnd));

    // Strings the game sets for the message can be used on any page.
    std::set<int> strings;
    for (size_t i = 0; i + 1 < b.size();) {
        if (b[i] == kCharControl && is_placeholder_code(b[i + 1])) {
            strings.insert(b[i + 1]);
        }
        i += code_size(&b[i], b.size() - i);
    }
    // SELECT_STRING_2..6 / SELECT_WINDOW in the tail: the last page sets up a choice.
    bool choice = false;
    for (size_t i = 0; i + 1 < t.tail.size();) {
        const uint8_t code = t.tail[i] == kCharControl ? t.tail[i + 1] : 0xFF;
        if (code == 13 || (code >= 22 && code <= 24) || code == 121 || code == 122) {
            choice = true;
        }
        i += code_size(&t.tail[i], t.tail.size() - i);
    }
    for (auto& p : t.pages) {
        p.messageStrings = strings;
    }
    t.pages.back().beforeChoice = choice;
    return t;
}

bool looks_like_refusal(const std::string& text) {
    std::string s;
    for (char c : text) {
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const size_t start = s.find_first_not_of(" .,!-'");
    s = start == std::string::npos ? std::string() : s.substr(start);
    for (size_t pos; (pos = s.find("\xe2\x80\x99")) != std::string::npos;) {
        s.replace(pos, 3, "'");
    }
    static const char* kStarts[] = { "i cannot", "i can't", "i can not", "i'm not able", "i am not able",
                                     "i'm unable", "i am unable", "i won't", "i will not", "i'm sorry, but",
                                     "sorry, but i", "i apologize", "as an ai", "i must decline",
                                     "i don't feel comfortable", "i'm not comfortable" };
    for (const char* p : kStarts) {
        if (s.rfind(p, 0) == 0) {
            return true;
        }
    }
    return s.find("as an ai") != std::string::npos || s.find("language model") != std::string::npos ||
           s.find("in the villager's voice") != std::string::npos;
}

std::string normalize_punctuation(const std::string& s) {
    static const std::pair<const char*, const char*> kMap[] = {
        { "\xE2\x80\x98", "'" }, { "\xE2\x80\x99", "'" }, { "\xE2\x80\x9C", "\"" }, { "\xE2\x80\x9D", "\"" },
        { "\xE2\x80\x93", "-" }, { "\xE2\x80\x94", "-" }, { "\xE2\x80\xA6", "..." }, { "\xC2\xA0", " " },
    };
    std::string out = s;
    for (const auto& [from, to] : kMap) {
        for (size_t pos = 0; (pos = out.find(from, pos)) != std::string::npos; pos += std::strlen(to)) {
            out.replace(pos, std::strlen(from), to);
        }
    }
    return out;
}

namespace {

// "a {a_item0}" -> "a {item0}": the model doubled the article.
std::string fix_articles(std::string s) {
    static const char* kWords[] = { "a",   "an",   "the",  "this", "that", "my",   "your", "his",
                                    "her", "our",  "their", "some", "its",  "these", "those" };
    for (size_t pos = 0; (pos = s.find("{a_item", pos)) != std::string::npos;) {
        size_t wordEnd = pos;
        while (wordEnd > 0 && s[wordEnd - 1] == ' ') {
            --wordEnd;
        }
        size_t wordStart = wordEnd;
        while (wordStart > 0 && std::isalpha(static_cast<unsigned char>(s[wordStart - 1]))) {
            --wordStart;
        }
        std::string word = s.substr(wordStart, wordEnd - wordStart);
        std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool doubled =
            wordEnd < pos && std::any_of(std::begin(kWords), std::end(kWords), [&](const char* w) { return word == w; });
        if (doubled) {
            s.erase(pos + 1, 2); // "{a_item" -> "{item"
        }
        pos += 1;
    }
    return s;
}

bool ends_with_question(const std::string& s) {
    const std::string plain = without_placeholders(s);
    size_t end = plain.find_last_not_of(" .!\"'");
    return end != std::string::npos && plain[end] == '?';
}

} // namespace

std::optional<std::string> validate_rewrite(const Page& page, const std::string& text) {
    std::string s = collapse(normalize_punctuation(text));
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = collapse(s.substr(1, s.size() - 2));
    }
    s = fix_articles(s);
    if (s.size() < 2 || s.size() > 320) {
        return std::nullopt;
    }
    if (std::islower(static_cast<unsigned char>(s.front())) && !page.text.empty() &&
        !std::islower(static_cast<unsigned char>(page.text.front()))) {
        return std::nullopt; // started mid-sentence
    }
    // A question the player answers by picking a choice has to stay a question.
    if (page.beforeChoice && ends_with_question(page.text)) {
        if (!ends_with_question(s)) {
            return std::nullopt;
        }
        // ...and the real one, not a trailing "Did you know...?" pet phrase.
        std::string tail = s.substr(s.size() > 20 ? s.size() - 20 : 0);
        std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool petPhrase = tail.find("you know...?") != std::string::npos || tail.find("you know?") != std::string::npos;
        if (petPhrase && page.text.find("know?") == std::string::npos) {
            return std::nullopt;
        }
    }
    // Same inline strings, the same number of times (items may switch article form;
    // the player's name and catchphrase are only flavor and may come and go).
    auto counts = [](const std::string& str, std::map<int, int>& out) {
        bool known = true;
        const bool balanced = for_each_placeholder(str, [&](const std::string& key) {
            uint8_t code = 0;
            bool cut = false;
            if (parse_key(key, code, cut)) {
                ++out[code];
            } else {
                known = false;
            }
        });
        return balanced && known;
    };
    std::map<int, int> want, got;
    if (!counts(page.text, want) || !counts(s, got)) {
        return std::nullopt;
    }
    for (int flavor : { int(kPlayerName), int(kTail) }) {
        if (got[flavor] > want[flavor] + 1) {
            return std::nullopt;
        }
        want.erase(flavor);
        got.erase(flavor);
    }
    // Everything the page mentions (items, prices, names) must still be mentioned;
    // anything else the game set for this message may be mentioned too.
    for (const auto& [code, n] : want) {
        if (n > 0 && got[code] == 0) {
            return std::nullopt;
        }
    }
    for (const auto& [code, n] : got) {
        if (n > 0 && !page.messageStrings.count(code)) {
            return std::nullopt;
        }
    }
    const std::string plain = without_placeholders(s);
    for (const auto& n : numbers_in(without_placeholders(page.text))) {
        if (plain.find(n) == std::string::npos) {
            return std::nullopt; // a price or count went missing
        }
    }
    if (s.size() > std::max<size_t>(page.text.size() * 2 + 40, 220)) {
        return std::nullopt; // rambling
    }
    return s;
}

namespace {

// One word (no spaces) of UTF-8 with {placeholders} as game bytes and pixel width.
void encode_word(const std::string& word, ResolveFn resolve, void* user, std::vector<uint8_t>& bytes, int& width) {
    const std::string text = normalize_punctuation(word);
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i);
            if (close != std::string::npos) {
                std::vector<uint8_t> code;
                int w = 0;
                if (resolve && resolve(text.substr(i + 1, close - i - 1), code, w, user)) {
                    bytes.insert(bytes.end(), code.begin(), code.end());
                    width += w;
                }
                i = close + 1;
                continue;
            }
        }
        size_t consumed = 1;
        const int code = acgc::text::decode(std::string_view(text).substr(i), consumed);
        i += std::max<size_t>(consumed, 1);
        if (code < 0 || code > 0xFF || code == kCharControl || code == kCharMessageTag || code == kCharNewLine ||
            code == kCharSpace) {
            continue; // no glyph in the game font
        }
        bytes.push_back(static_cast<uint8_t>(code));
        width += mFont_GetCodeWidth(static_cast<uint8_t>(code), 1);
    }
}

} // namespace

void LineStreamer::commit_word(const std::string& word, std::vector<uint8_t>& out, bool last) {
    if (m_text.empty() && !m_dropLeading.empty() && word == m_dropLeading) {
        m_dropLeading.clear();
        return;
    }
    // A "word" of only punctuation ("??", "!!!", ",?") is what a model writes when the grammar
    // blocked the word it wanted. Mid-line it's dropped; at the very end its last mark is attached
    // to the previous word. "..." and "-" are real.
    const bool punctOnly = !word.empty() && word.find('{') == std::string::npos &&
                           std::none_of(word.begin(), word.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)); });
    if (punctOnly && word != "..." && word != "-") {
        const char mark = word.back();
        if (!last || m_linesOnPage == 0 || std::strchr(".!?", mark) == nullptr) {
            return;
        }
        const int w = mFont_GetCodeWidth(static_cast<uint8_t>(mark), 1);
        if (m_lineWidth + w <= kLineWidth) {
            out.push_back(static_cast<uint8_t>(mark));
            m_lineWidth += w;
            m_text += mark;
        }
        return;
    }
    std::vector<uint8_t> bytes;
    int width = 0;
    encode_word(word, m_resolve, m_user, bytes, width);
    if (bytes.empty()) {
        return;
    }
    if (m_linesOnPage == 0 && m_marker >= 0) {
        bytes.insert(bytes.begin(), static_cast<uint8_t>(m_marker));
        width += mFont_GetCodeWidth(static_cast<uint8_t>(m_marker), 1);
    }
    const int spaceWidth = mFont_GetCodeWidth(kCharSpace, 1);
    if (m_linesOnPage == 0) {
        m_linesOnPage = 1;
    } else if (m_lineWidth + spaceWidth + width > kLineWidth) {
        if (m_linesOnPage == kLinesPerPage) {
            out.insert(out.end(), { kCharControl, kButton, kCharControl, kClear });
            m_linesOnPage = 1;
        } else {
            out.push_back(kCharNewLine);
            ++m_linesOnPage;
        }
        m_lineWidth = 0;
    } else {
        out.push_back(kCharSpace);
        m_lineWidth += spaceWidth;
    }
    out.insert(out.end(), bytes.begin(), bytes.end());
    m_lineWidth += width;
    if (!m_text.empty()) {
        m_text += ' ';
    }
    m_text += word;
}

std::vector<uint8_t> LineStreamer::feed(std::string_view piece) {
    std::vector<uint8_t> out;
    for (char c : piece) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (!m_pending.empty()) {
                commit_word(m_pending, out);
                m_pending.clear();
            }
        } else {
            m_pending += c;
        }
    }
    return out;
}

std::vector<uint8_t> LineStreamer::finish(bool endMessage) {
    std::vector<uint8_t> out;
    // A line cut off at the length cap can end inside a placeholder ("{pla"): drop that part.
    if (const size_t open = m_pending.rfind('{'); open != std::string::npos && m_pending.find('}', open) == std::string::npos) {
        m_pending.erase(open);
    }
    if (!m_pending.empty()) {
        commit_word(m_pending, out, /*last=*/true);
        m_pending.clear();
    }
    if (endMessage) {
        out.insert(out.end(), { kCharControl, kLast });
    }
    return out;
}

std::vector<std::vector<uint8_t>> wrap_text(const std::string& utf8, ResolveFn resolve, void* user) {
    struct Word {
        std::vector<uint8_t> bytes;
        int width = 0;
    };
    std::vector<Word> words;
    Word cur;
    auto flush = [&] {
        if (!cur.bytes.empty()) {
            words.push_back(std::move(cur));
        }
        cur = {};
    };

    const std::string text = normalize_punctuation(utf8);
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i);
            if (close != std::string::npos) {
                std::vector<uint8_t> bytes;
                int width = 0;
                if (resolve && resolve(text.substr(i + 1, close - i - 1), bytes, width, user)) {
                    cur.bytes.insert(cur.bytes.end(), bytes.begin(), bytes.end());
                    cur.width += width;
                }
                i = close + 1;
                continue;
            }
        }
        const char c = text[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            flush();
            ++i;
            continue;
        }
        size_t consumed = 1;
        const int code = acgc::text::decode(std::string_view(text).substr(i), consumed);
        i += std::max<size_t>(consumed, 1);
        if (code < 0 || code > 0xFF || code == kCharControl || code == kCharMessageTag || code == kCharNewLine) {
            continue; // no glyph in the game font
        }
        cur.bytes.push_back(static_cast<uint8_t>(code));
        cur.width += mFont_GetCodeWidth(static_cast<uint8_t>(code), 1);
    }
    flush();

    std::vector<std::vector<uint8_t>> lines;
    int lineWidth = 0;
    const int spaceWidth = mFont_GetCodeWidth(kCharSpace, 1);
    for (auto& word : words) {
        if (lines.empty() || (lineWidth + spaceWidth + word.width > kLineWidth)) {
            lines.emplace_back();
            lineWidth = 0;
        } else {
            lines.back().push_back(kCharSpace);
            lineWidth += spaceWidth;
        }
        lines.back().insert(lines.back().end(), word.bytes.begin(), word.bytes.end());
        lineWidth += word.width;
    }
    return lines;
}

std::vector<uint8_t> paginate(const std::vector<std::vector<uint8_t>>& lines) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            if (i % kLinesPerPage == 0) {
                out.insert(out.end(), { kCharControl, kButton, kCharControl, kClear });
            } else {
                out.push_back(kCharNewLine);
            }
        }
        out.insert(out.end(), lines[i].begin(), lines[i].end());
    }
    return out;
}

RewriteRequest rewrite_request(const Template& t, const std::string& personalityId, const std::string& voice,
                               const std::string& situation, const std::string& setting,
                               const std::string& toneRules) {
    RewriteRequest r;
    r.system =
        "You restyle dialog for a villager in " + setting + ". You get one message the villager says, split into pages (each page fills its own "
        "text box). Edit every page so it sounds like this villager: keep the original sentences and what "
        "they say, and change the wording and tone to fit the personality. You may add one short phrase of "
        "flavor per page; don't add sentences about other topics.\n"
        "Rules:\n" +
        toneRules +
        "- Keep what matters for what happens next: what is being offered, given, sold, bought, asked for or "
        "shown, and every price and number. If the villager offers a shirt, the edit still offers the shirt.\n"
        "- The villager is the one speaking, to the player (\"you\"). Never swap who gives, sells, buys, "
        "wants or shows what: if the villager wants to buy the player's item, the rewrite still has the "
        "villager buying it.\n"
        "- Keep every {placeholder} the original page has; the game fills them in. {player} is the player's "
        "name, {catchphrase} is the villager's catchphrase (it usually ends a sentence), {a_item0} is an item "
        "with its article (\"a pear\"), {item0} is the same item without the article (\"pear\"), so never "
        "write \"a {a_item0}\". {free1} and similar are names, places or amounts; the original page shows "
        "what each one means. Don't make up new placeholders.\n"
        "- If the last page ends with a question, keep it a question with the same meaning: the player "
        "answers it by picking from fixed choices.\n"
        "- One page in, one page out, each about as long as the original and never more than 170 "
        "characters. Use the personality's pet phrases sparingly. Plain text, no emoji, no quotation marks "
        "around pages.\n"
        "- Never mention being an AI or a game.\n"
        "Example. Original page: \"Would you sell it to me for {free19} Bells? Please?\"\n"
        "A grumpy villager: \"Hmph. I'll give you {free19} Bells for it. Deal or not?\"\n"
        "A bubbly villager: \"Ooh! Would you sell it to me for {free19} Bells? Pretty please?\"";

    std::string user = "Villager personality (" + personalityId + "): " + voice + "\n";
    if (!situation.empty()) {
        user += "What's going on: " + situation + "\n";
    }
    user += "\nOriginal pages:\n";
    size_t chars = 0;
    for (size_t i = 0; i < t.pages.size(); ++i) {
        if (t.pages[i].rewritable) {
            r.pages.push_back(i);
            user += std::to_string(r.pages.size()) + ". " + t.pages[i].text + "\n";
            chars += t.pages[i].text.size();
        }
    }
    const size_t n = r.pages.size();
    user += "\nReply with JSON only: {\"pages\": [...]} with exactly " + std::to_string(n) +
            (n == 1 ? " string" : " strings") + ", one per page, in order.";
    r.user = std::move(user);

    r.grammar = "root ::= \"{\" ws \"\\\"pages\\\"\" ws \":\" ws \"[\" ws page";
    if (n > 1) {
        r.grammar += " (ws \",\" ws page){" + std::to_string(n - 1) + "}";
    }
    r.grammar += " ws \"]\" ws \"}\"\n"
                 "page ::= \"\\\"\" char{2,300} \"\\\"\"\n"
                 "char ::= [^\"\\\\\\x00-\\x1F] | \"\\\\\" [\"\\\\/bfnrt]\n"
                 "ws   ::= [ \\t\\n]{0,4}\n";
    // ~4 characters per token, room for a longer rewrite and the JSON around it.
    r.maxTokens = static_cast<int>(std::clamp<size_t>(chars + 120 * n, 200, 2400));
    return r;
}

std::vector<uint8_t> original_from(const Template& t, size_t page) {
    std::vector<uint8_t> out;
    for (size_t i = page; i < t.pages.size(); ++i) {
        if (i > page) {
            out.insert(out.end(), t.separators[i - 1].begin(), t.separators[i - 1].end());
        }
        out.insert(out.end(), t.pages[i].original.begin(), t.pages[i].original.end());
    }
    out.insert(out.end(), t.tail.begin(), t.tail.end());
    return out;
}

std::set<int> string_codes(const Template& t) {
    return t.pages.empty() ? std::set<int>{} : t.pages.front().messageStrings;
}

bool asks_choice(const Page& page) { return page.beforeChoice && ends_with_question(page.text); }

std::string live_page_system(const std::string& setting, const std::string& toneRules) {
    return "You voice a villager in " + setting +
           ". The villager is talking with the player. You get one page of what the villager says (one text "
           "box) as the game wrote it, and say that same page again in the villager's own voice.\n"
           "Rules:\n" +
           toneRules +
           "- Keep the original's sentences: one sentence for each of its sentences, in order, each saying what "
           "that one says. Put the personality into the wording of those sentences (attitude, slang, swearing, "
           "insults inside them), not into extra sentences; you may start with an interjection of a few words. "
           "No new facts, names, events or stories. The next pages are handled separately, so never get ahead of "
           "them.\n"
           "- Keep everything that matters for what happens next: what is offered, given, sold, bought, asked "
           "for or shown, every price and number, and who does what. The villager is the one speaking, to the "
           "player (\"you\").\n"
           "- Write every {placeholder} of the original page exactly as written and in the same order; the game "
           "fills them in. {player} is the player's name, {catchphrase} is the villager's catchphrase, {a_item0} "
           "is an item with its article (\"a pear\"), {item0} the same item without it, so never write \"a "
           "{a_item0}\". {free1} and similar are names, places or amounts: the original shows what each one "
           "means. Never make up new placeholders.\n"
           "- If the page ends with a question, end with the same question: the player answers it by picking "
           "from fixed choices.\n"
           "- Plain spoken words only: no emoji, no quotation marks, no actions or narration.\n"
           "- Never mention being an AI or a game, and never comment on the task: just say the page.\n"
           "Examples.\n"
           "Original: Would you sell it to me for {free19} Bells? Please?\n"
           "A grumpy villager: Hmph. I'll give you {free19} Bells for it. Deal or not?\n"
           "A bubbly villager: Ooh! Would you sell it to me for {free19} Bells? Pretty please?\n"
           "Original: I got this letter from {free15}.\n"
           "A grumpy villager: Got this letter from {free15}. Don't ask me why.";
}

namespace {

// One sentence of a page: its text, and how it ends ('?' or '!' or '.', or 0 for a fragment).
struct Sentence {
    std::string text;
    char end = 0;
};

// Splits page text into sentences at runs of . ! ? (not inside {placeholders}).
std::vector<Sentence> split_sentences(const std::string& text) {
    std::vector<Sentence> out;
    std::string cur;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        depth += ch == '{' ? 1 : ch == '}' ? -1 : 0;
        cur += ch;
        if (depth == 0 && std::strchr(".!?", ch) != nullptr) {
            while (i + 1 < text.size() && std::strchr(".!?'\")", text[i + 1]) != nullptr) {
                cur += text[++i];
            }
            const bool q = cur.find('?', cur.find_last_not_of(".!?'\")") + 1) != std::string::npos;
            const bool x = cur.find('!', cur.find_last_not_of(".!?'\")") + 1) != std::string::npos;
            out.push_back({ collapse(cur), q ? '?' : x ? '!' : '.' });
            cur.clear();
        }
    }
    if (const std::string rest = collapse(cur); !rest.empty()) {
        out.push_back({ rest, 0 });
    }
    return out;
}

int word_count_of(const std::string& s) {
    const std::string plain = collapse(without_placeholders(s));
    return plain.empty() ? 0 : 1 + static_cast<int>(std::count(plain.begin(), plain.end(), ' '));
}

} // namespace

LivePageRequest live_page_request(const Page& page, const std::string& system, const std::string& context) {
    LivePageRequest r;
    r.system = system;
    const auto sentences = split_sentences(page.text);
    r.user = context + "\nThe page to say now (say only this, in the villager's voice; it has " +
             std::to_string(sentences.size()) + (sentences.size() == 1 ? " sentence" : " sentences") +
             ", so say " + (sentences.size() == 1 ? "one" : "the same number") + "): " + page.text + "\n\nSay it now.";

    // The page's sentences, one for one and in order, each with a word cap of about twice the
    // original's and the placeholders that carry meaning (items, prices, names) that it had.
    // {player} and {catchphrase} are flavor and may go anywhere. An optional interjection of up
    // to 6 words may come first. A page the player answers with a choice ends on "?". Words are
    // whole (a cap never cuts one), and nothing else can make up a placeholder.
    std::string g = "root ::= (intro \" \")?";
    // Printable ASCII only (the font has no glyphs for most else), minus space, quote, asterisk,
    // backslash and braces.
    std::string rules = "c ::= [\\x21\\x23-\\x29\\x2B-\\x5B\\x5D-\\x7A\\x7C\\x7E] | \"{player}\" | \"{catchphrase}\"\n"
                        "w ::= c{1,12}\n"
                        "intro ::= w (\" \" w){0,5} [,.!?]\n";
    const bool choice = asks_choice(page);
    int totalWords = 6;
    for (size_t k = 0; k < sentences.size(); ++k) {
        const auto& sen = sentences[k];
        std::vector<std::string> keys;
        for_each_placeholder(sen.text, [&](const std::string& key) {
            if (key != "player" && key != "catchphrase") {
                keys.push_back(key);
            }
        });
        // Word caps for the stretches between this sentence's forced placeholders.
        std::vector<int> stretchWords;
        {
            size_t from = 0;
            for (const auto& key : keys) {
                const size_t at = sen.text.find("{" + key + "}", from);
                stretchWords.push_back(word_count_of(sen.text.substr(from, at == std::string::npos ? 0 : at - from)));
                from = at == std::string::npos ? from : at + key.size() + 2;
            }
            stretchWords.push_back(word_count_of(sen.text.substr(std::min(from, sen.text.size()))));
        }
        const std::string name = "t" + std::to_string(k);
        g += (k == 0 ? " " : " \" \" ") + name;
        std::string rule = name + " ::=";
        for (size_t p = 0; p < stretchWords.size(); ++p) {
            const int cap = std::max(3, stretchWords[p] * 3 + 8); // a safety net, not a target
            totalWords += cap;
            const std::string words = "(\" \" w){0," + std::to_string(cap - 1) + "}";
            if (p < keys.size()) {
                rule += " \" \"? (w " + words + ")? \" \"? \"{" + keys[p] + "}\"";
            } else if (stretchWords[p] > 0) {
                rule += " \" \"? w " + words; // ends on a word: the end mark follows it directly
            }
        }
        const bool last = k + 1 == sentences.size();
        if (last && choice) {
            rule += " \"?\"";
        } else if (sen.end != 0) {
            rule += " [.!?]{1,3}";
        } else {
            rule += " [.!?]{0,3}";
        }
        rules += rule + "\n";
    }
    r.grammar = g + "\n" + rules;
    r.maxChars = totalWords * 7;
    r.maxTokens = totalWords * 2 + 16;
    return r;
}

std::optional<std::vector<uint8_t>> build(const Template& t, const std::vector<std::optional<std::string>>& pages,
                                          WidthFn width, void* user, size_t maxSize) {
    std::vector<uint8_t> out;
    bool changed = false;
    for (size_t i = 0; i < t.pages.size(); ++i) {
        const Page& page = t.pages[i];
        if (i > 0) {
            out.insert(out.end(), t.separators[i - 1].begin(), t.separators[i - 1].end());
        }
        std::vector<std::vector<uint8_t>> lines;
        if (page.rewritable && i < pages.size() && pages[i]) {
            PageResolver ctx{ &page, width, user };
            lines = wrap_text(*pages[i], &resolve_page_placeholder, &ctx);
        }
        if (lines.empty()) {
            out.insert(out.end(), page.original.begin(), page.original.end());
            continue;
        }
        out.insert(out.end(), page.lead.begin(), page.lead.end());
        const auto body = paginate(lines);
        out.insert(out.end(), body.begin(), body.end());
        out.insert(out.end(), page.trail.begin(), page.trail.end());
        changed = true;
    }
    out.insert(out.end(), t.tail.begin(), t.tail.end());
    if (!changed || out.size() > maxSize) {
        return std::nullopt;
    }
    return out;
}

} // namespace acgc::dialog::msg

extern "C" void pc_dialog_set_message_archive(const uint8_t* table, uint32_t table_size, const uint8_t* data,
                                              uint32_t data_size) {
    acgc::dialog::msg::set_archive(table, table_size, data, data_size);
}
