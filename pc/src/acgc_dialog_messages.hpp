// Read-only access to the game's original messages, for writing villager-voiced
// rewrites of them (see acgc_dialog.cpp). The archive is copied once at boot.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace acgc::dialog::msg {

constexpr uint8_t kCharControl = 0x7F;
constexpr uint8_t kCharMessageTag = 0x80;

void set_archive(const uint8_t* table, uint32_t tableSize, const uint8_t* data, uint32_t dataSize);
bool available();
int count();

// Bytes of message `index`, cut after its end code. nullopt if out of range.
std::optional<std::vector<uint8_t>> original(int index);

// Size in bytes of the character or control code starting at `p` (never 0).
int code_size(const uint8_t* p, size_t remaining);
const char* code_name(uint8_t code);

// Readable form: text as UTF-8, codes as <NAME arg..>, new lines as "\n".
std::string describe(const std::vector<uint8_t>& bytes);

void dump_all(const std::filesystem::path& path);

// Message numbers this message can branch or continue to (NEXT_* codes).
std::vector<int> next_messages(const std::vector<uint8_t>& bytes);

uint64_t fingerprint(const std::vector<uint8_t>& bytes);

// ---------------------------------------------------------------------------
// Rewrite templates. A message is split into pages (text boxes the player
// advances with A). Each page's words can be replaced; every control code that
// does something (items, prices, choices, branches, quest steps, animations) is
// kept and put back around the new words.

struct Page {
    std::vector<uint8_t> lead;     // kept codes, emitted before the text
    std::vector<uint8_t> trail;    // kept codes, emitted after the text
    std::vector<uint8_t> original; // the page as the game has it
    std::string text;              // UTF-8, inline strings as {placeholders}
    bool rewritable = false;
    bool beforeChoice = false;     // the player picks from choices right after this page
    std::set<int> messageStrings;  // inline string codes anywhere in the message (all usable)
};

struct Template {
    std::vector<Page> pages;
    std::vector<std::vector<uint8_t>> separators; // between pages (button + clear)
    std::vector<uint8_t> tail;                    // choices, branches, end code
    int rewritable_pages() const;
};

std::optional<Template> parse_template(const std::vector<uint8_t>& bytes);

// Checks a model rewrite of `page` and normalizes it. nullopt if it drops or
// adds a placeholder, loses a number, or is far off the original's length.
std::optional<std::string> validate_rewrite(const Page& page, const std::string& text);

// Width in pixels of an inline string code when it prints; -1 = unknown.
using WidthFn = int (*)(uint8_t code, bool cutArticle, void* user);

// Turns a {placeholder} name into game bytes and a print width. false = drop it.
using ResolveFn = bool (*)(const std::string& key, std::vector<uint8_t>& bytes, int& width, void* user);

// Word-wraps UTF-8 text with {placeholders} into game-encoded lines of the
// dialog box width.
std::vector<std::vector<uint8_t>> wrap_text(const std::string& text, ResolveFn resolve, void* user);

// Replaces typographic punctuation the game font has no glyph for.
std::string normalize_punctuation(const std::string& s);

// Joins lines into pages of 4 (button + clear between pages).
std::vector<uint8_t> paginate(const std::vector<std::vector<uint8_t>>& lines);

// The model request for rewriting a template's pages in a personality's voice.
// The reply is {"pages": [...]}, one string per entry in `pages` (template page indices).
struct RewriteRequest {
    std::string system;
    std::string user;
    std::string grammar; // GBNF for the embedded model
    std::vector<size_t> pages;
    int maxTokens = 0;
};
// `situation` (may be empty) says what's going on, e.g. "the villager wants to buy the player's item".
RewriteRequest rewrite_request(const Template& t, const std::string& personalityId, const std::string& voice,
                               const std::string& situation);

// Builds the full message: rewritten pages where given (and valid), original pages elsewhere.
std::optional<std::vector<uint8_t>> build(const Template& t, const std::vector<std::optional<std::string>>& pages,
                                          WidthFn width, void* user, size_t maxSize);

} // namespace acgc::dialog::msg
