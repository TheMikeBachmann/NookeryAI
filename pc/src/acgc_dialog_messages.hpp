// Read-only access to the game's original messages, for writing villager-voiced
// rewrites of them (see acgc_dialog.cpp). The archive is copied once at boot.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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

// The DEMO_ORDER_QUEST codes in a message (quest and event triggers), in order, so a message
// replaced outright by generated text still triggers them.
std::vector<uint8_t> quest_codes(const std::vector<uint8_t>& bytes);

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

// True when model output starts like a refusal or a comment on the task ("I cannot...",
// "As an AI..."), not something a villager would say.
bool looks_like_refusal(const std::string& text);

// The message a <NEXT_F n> code in `bytes` jumps to, or -1 if there's none.
int next_target(const std::vector<uint8_t>& bytes);
// Points every <NEXT_F n> in `bytes` at `to`. False if there was none.
bool retarget_next(std::vector<uint8_t>& bytes, int to);
// The bytes of <NEXT_F to><FORCE_NEXT><CONTINUE>: jump straight on to message `to`.
std::vector<uint8_t> jump_to(int to);
// About how many words are spoken in `bytes` (text only, codes skipped).
int spoken_words(const std::vector<uint8_t>& bytes);
// spoken_words over message `first` and everything it jumps on to with <NEXT_F n>.
int chain_words(int first);

// True when the end of `text` is the model stuck in a loop: the same phrase of 3+ words twice
// in a row, or the same word or pair four times.
bool is_looping(const std::string& text);

// Replaces typographic punctuation the game font has no glyph for.
std::string normalize_punctuation(const std::string& s);

// Joins lines into pages of 4 (button + clear between pages).
std::vector<uint8_t> paginate(const std::vector<std::vector<uint8_t>>& lines);

// Lays out text that arrives a piece at a time (a model streaming its reply) as
// game bytes: whole words only, wrapped and paged like wrap_text + paginate.
class LineStreamer {
public:
    LineStreamer(ResolveFn resolve, void* user) : m_resolve(resolve), m_user(user) {}
    // Bytes for every word completed by `piece`.
    std::vector<uint8_t> feed(std::string_view piece);
    // Bytes for the last word plus the end-of-message code (or without it, for one page of a
    // longer message).
    std::vector<uint8_t> finish(bool endMessage = true);
    bool empty() const { return m_linesOnPage == 0; }
    // Drops the first word if it's exactly this (a model starting with "Bill:").
    void drop_leading(std::string word) { m_dropLeading = std::move(word); }
    // A game character put in front of the first word (not part of text()).
    void set_marker(uint8_t code) { m_marker = code; }
    // The text committed so far (UTF-8), for logs and history.
    const std::string& text() const { return m_text; }

private:
    // Holds the first words back while they might be the model introducing its answer ("Here's
    // what the villager says in their own voice:"), which is dropped.
    void take_word(const std::string& word, std::vector<uint8_t>& out, bool last = false);
    void flush_held(std::vector<uint8_t>& out, bool last);
    void commit_word(const std::string& word, std::vector<uint8_t>& out, bool last = false);

    ResolveFn m_resolve;
    void* m_user;
    bool m_preambleCheck = true;
    std::vector<std::string> m_held;
    std::string m_pending;
    std::string m_dropLeading;
    std::string m_text;
    int m_marker = -1;
    int m_lineWidth = 0;
    int m_linesOnPage = 0; // 0 = nothing written yet
};

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
// `setting` describes the game ("a cozy, all-ages village life game where..."); `toneRules` is
// zero or more "- rule\n" lines about content and tone (see "tone" in personalities.jsonc).
RewriteRequest rewrite_request(const Template& t, const std::string& personalityId, const std::string& voice,
                               const std::string& situation, const std::string& setting,
                               const std::string& toneRules);

// ---------------------------------------------------------------------------
// Live rewrites: a message rewritten page by page as it's shown, streamed into the text box.

// Turns a page's {placeholders} into game codes, with CUT_ARTICLE where the original page had it.
// Use with LineStreamer; `user` is a PageResolver*.
struct PageResolver {
    const Page* page = nullptr;
    WidthFn width = nullptr;
    void* user = nullptr;
};
bool resolve_page_placeholder(const std::string& key, std::vector<uint8_t>& bytes, int& width, void* user);

// The message as the game wrote it from page `i` on (that page's original bytes, then the rest
// and the tail): what plays if a live rewrite can't finish.
std::vector<uint8_t> original_from(const Template& t, size_t page);

// Inline-string codes ({player}, {free15}...) a message uses, for measuring them up front.
std::set<int> string_codes(const Template& t);

// Whether the page asks the question the player answers by picking a choice right after it.
bool asks_choice(const Page& page);

// The model request for rewriting one page live. The system prompt is the same for every page
// (so the model's cache of it is reused); `user` carries the page and its context.
struct LivePageRequest {
    std::string system;
    std::string user;
    std::string grammar; // every placeholder of the original, in order; a "?" at the end if it asks a choice
    int maxTokens = 0;
    int maxChars = 0;
};
std::string live_page_system(const std::string& setting, const std::string& toneRules);

// The GBNF rules a rewrite of `page` must follow (its sentences, placeholders and length caps),
// with every rule name starting with `prefix` so several pages can share one grammar. `top` is
// the rule for the whole page; there's no "root" unless `prefix` is empty.
struct PageGrammar {
    std::string top;
    std::string rules;
    int sentences = 0;
    int totalWords = 0; // the page's word cap
    int maxChars = 0;   // free_page_grammar: the page's character cap
};
PageGrammar page_grammar(const Page& page, const std::string& prefix);
// A looser, much faster grammar for the same page: free text with the page's meaningful
// placeholders in order, a length cap (at most `maxChars` if that's given), and a "?" at the end
// if the player picks a choice next.
PageGrammar free_page_grammar(const Page& page, const std::string& prefix, int maxChars = 0);

// Only the codes of `bytes` that don't print or wait for the player (music, animations, sounds,
// branches): what must still run when the text they came with is skipped.
std::vector<uint8_t> codes_only(const std::vector<uint8_t>& bytes);
LivePageRequest live_page_request(const Page& page, const std::string& system, const std::string& context);

// Builds the full message: rewritten pages where given (and valid), original pages elsewhere.
std::optional<std::vector<uint8_t>> build(const Template& t, const std::vector<std::optional<std::string>>& pages,
                                          WidthFn width, void* user, size_t maxSize);

} // namespace acgc::dialog::msg
