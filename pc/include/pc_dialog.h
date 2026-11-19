#ifndef PC_DIALOG_H
#define PC_DIALOG_H

/*
 * Generated villager dialog.
 *
 * Small talk: when a villager picks a plain small-talk message, it is replaced
 * by a line from that villager's pool. A background worker keeps the pools
 * filled with lines a language model writes from each personality's web sources.
 *
 * Everything else a villager says (trades, letters, memories, hints...) is
 * rewritten in their personality's voice, message by message. The game's control
 * codes (items, prices, choices, branches) are kept exactly; only the words
 * change. Rewrites are made in the background and saved; until one is ready,
 * the original message plays.
 *
 * Config: <data folder>/dialog/personalities.json (written with defaults on first run).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_DIALOG_NAME_LEN 8
#define PC_DIALOG_CATCHPHRASE_LEN 10

typedef struct {
    /* All strings are in the game's character encoding, not UTF-8. */
    uint8_t name[PC_DIALOG_NAME_LEN];
    int name_len;
    uint8_t catchphrase[PC_DIALOG_CATCHPHRASE_LEN];
    int catchphrase_len;
    int looks; /* mNpc_LOOKS_* built-in personality */
    int mood;  /* mNpc_FEEL_* */
} PCDialogVillager;

/* Tells the worker a villager exists so it can start filling their pool. Cheap; safe to repeat. */
void pc_dialog_register_villager(const PCDialogVillager* villager);

/*
 * Called when the game has picked chit-chat message `msg_no` for `villager`.
 * If a generated line is ready, it is prepared for the next load of `msg_no`
 * and 1 is returned; otherwise 0 and the original message plays.
 * `player_name` is the current player's name in game encoding.
 */
int pc_dialog_arm(int msg_no, const PCDialogVillager* villager, const uint8_t* player_name, int player_name_len);

/*
 * Called from the message loader for every message. If a line is armed for
 * `msg_no`, writes it to `dst` and returns its length (the line is used once);
 * otherwise returns -1.
 *
 * A live line is still being written by the model when this is called: the
 * length may be 0 and there is no end code yet. The rest arrives through
 * pc_dialog_stream_pump.
 */
int pc_dialog_take(int msg_no, uint8_t* dst, int dst_size);

/*
 * Called by the message cursor every time it is about to read the character at
 * `idx`. Appends whatever the model has written since the last call at
 * `*len` (the end of the game's buffer) and updates `*len`. Returns 1 if the
 * cursor has caught up with the model and must wait, 0 to carry on.
 */
int pc_dialog_stream_pump(uint8_t* buf, int* len, int idx, int buf_size);

/* What the game world looks like right now, for the prompts. */
typedef struct {
    int weather; /* mEnv_WEATHER_*: clear, rain, snow, sakura, leaves */
    int month;   /* 1-12 */
    int day;     /* 1-31 */
    int weekday; /* 0 = Sunday */
    int hour;    /* 0-23 */
    int minute;
} PCDialogScene;

/*
 * Called when the player starts a conversation with a town villager. Every
 * message that villager (`actor`) says until the window closes can be swapped
 * for a rewrite in their personality's voice (see pc_dialog_rewrite).
 */
void pc_dialog_begin_talk(const PCDialogVillager* villager, const void* actor, const PCDialogScene* scene,
                          const uint8_t* player_name, int player_name_len);

/*
 * Called when a conversation started with a message that isn't replaced (not small talk),
 * so the talk log shows every conversation. `kind` says what it is, in plain English.
 */
void pc_dialog_note_talk(int msg_no, const char* kind);

/* Starts the service (and loading the model) early, so the first conversation doesn't wait. */
void pc_dialog_start(void);

/* Called when the message window lets go of its speaker. */
void pc_dialog_end_talk(void);

/*
 * Mr. Resetti (`actor`) starts talking. Everything he says until he leaves is rewritten
 * in the "resetti" personality's voice (personalities.json, "resetti" section), and an
 * off-script rant is spliced in before the last message of his lecture. Calling it again
 * for the same actor (his next talk on the same visit) keeps the visit going: one rant
 * per visit.
 */
void pc_dialog_begin_resetti(const void* actor, const uint8_t* player_name, int player_name_len);

/* Mr. Resetti leaves (his actor is going away). */
void pc_dialog_end_resetti(const void* actor);

/* Characters other than villagers whose dialog is rewritten in a personality of their own. */
enum {
    PC_DIALOG_CHARACTER_TOM_NOOK = 1, /* personalities.json, "tom_nook" section */
};

/*
 * One of those characters (`actor`) starts a talk. Everything said until the text box closes is
 * rewritten live in their personality's voice, keeping every code (menus, prices, choices).
 */
void pc_dialog_begin_character(int character, const void* actor, const uint8_t* player_name, int player_name_len);

/*
 * A generated phrase for the player to type as their apology, in game characters, padded
 * with spaces to `dst_size`. Returns 1 if one was ready, 0 if the game's own should be used.
 */
int pc_dialog_resetti_phrase(uint8_t* dst, int dst_size);

/*
 * Whether what the player typed (`typed`, `len` game characters, space padded) is the
 * phrase: letters and digits compared ignoring case, spaces and punctuation.
 */
int pc_dialog_phrase_matches(const uint8_t* typed, const uint8_t* phrase, int len);

/*
 * Message numbers a villager with built-in personality `looks` can open a
 * conversation with, all of one kind; `situation` says in plain English what is
 * going on (a trade, a letter, a memory...). The service follows each one's
 * branches and rewrites them in the background, telling the model the situation.
 */
void pc_dialog_register_roots(int looks, const char* situation, const int* msg_nos, int count);

/*
 * Called from the message loader for every message while `speaker_actor` is
 * talking. Returns -1 to play `msg_no` as the game wrote it. Otherwise `msg_no`
 * is shown in the current villager's voice: either a ready rewrite written to
 * `dst` (its length is returned), or a live rewrite (returns 0, or however much
 * is ready) whose pages arrive through pc_dialog_stream_pump as the model writes
 * them. Choices, branches and other codes are kept; if the model stalls, the rest
 * of the message plays as written.
 */
int pc_dialog_rewrite(int msg_no, const void* speaker_actor, uint8_t* dst, int dst_size);

/*
 * Implemented by the game (m_msg.c): the pixel width of what inline string code
 * `cont_code` (an item name, a free string...) will print right now, or -1 if
 * unknown. `cut_article` is non-zero when the text has a CUT_ARTICLE before it.
 */
int pc_dialog_string_code_width(int cont_code, int cut_article);

/*
 * Hands the service a copy of the game's message archive (the big-endian u32
 * end-offset table and the message bytes), read once on the main thread at boot.
 * The worker uses it to read original messages when writing rewrites.
 * Set NOOKERY_DUMP_MESSAGES=<file> to write every message as readable text.
 */
void pc_dialog_set_message_archive(const uint8_t* table, uint32_t table_size, const uint8_t* data, uint32_t data_size);

#ifdef __cplusplus
}
#endif

#endif
