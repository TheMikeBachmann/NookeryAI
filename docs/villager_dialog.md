# Generated villager dialog

Villagers talk in a voice you define. The goal is for every line a villager says to be written by
the model at runtime; phase 1 covers small talk only, so by default only small talk changes
(`rewrite_special` and `prewrite_small_talk` are off). Two things can change:

- **Small talk** is written live by a language model the moment you talk to a villager, and the
  words appear in the text box as they're generated. Each line can use the current weather, time
  and mood, and topics from web sources you choose for each personality (RSS/Atom feeds,
  Wikipedia, Reddit). Lines never run out or repeat.
- **Everything else a town villager says** (off by default, `rewrite_special`), such as trade offers, sales, gifts, letters, memories
  and tips, is rewritten in their personality's voice. The game's mechanics are untouched: items,
  prices, names, yes/no choices and what happens next all stay exactly as they were. Only the
  wording changes.

By default the model runs inside the game: llama.cpp is built in and runs on the GPU through
Vulkan, so no separate server is needed. A local server (Ollama, llama.cpp `llama-server`,
LM Studio) or the Claude API can be used instead.

Island villagers keep their original text. Anything not ready yet (first launch, no model, no
network) plays the original line.

## How it works

Everything else a villager says, live (`live_rewrites`, built-in model only):

1. When a town villager you're talking to loads any message that isn't small talk (trades, gifts,
   sales, letters, memories, tips, yes/no offers, first-job hints, and every branch they lead to),
   it's split into pages around the codes that do something: choices, branches, letter and quest
   triggers, animations, links to the next message.
2. Each page is rewritten in the villager's voice as it's shown, streamed into the text box like
   small talk; the next page is written while you read. The model gets the original page, what's
   going on (the conversation's opener says: a trade, a letter, a hint...), what the villager has
   said so far in this conversation, and their personality, catchphrase and tone rules.
3. The meaning stays the same. The model's output is constrained to include every item, price and
   name of the original page in order, to end on the same question when the player picks a choice
   next, and to stay within about twice the original's length. `{player}` and `{catchphrase}` may
   move. All the codes are put back exactly.
4. The first few words of every live line are held back until it's clear the model isn't refusing
   ("I can't..."). A refusal, a failure or a stall plays the rest of the message as the game wrote
   it, so choices and trades always work. Pages the rewriter can't safely touch (colored text, odd
   codes) stay as written.

Only conversations you start with a town villager are covered so far. Villagers who walk up to you,
errands and island villagers keep the game's text.


Small talk, live (`live_small_talk`, built-in model only):

1. When the game starts, a background worker loads the model onto the GPU and warms it up.
2. When a villager picks a small-talk line, the worker drops whatever background job it was doing
   and writes one line for that villager from their personality, mood, the game's current weather,
   time and date, a topic (see [Topics](#topics)), and their last few lines so they don't repeat
   themselves.
3. The words are word-wrapped and paged as they're generated and handed to the message window. If
   the window's text cursor catches up with the model, it waits a frame. With an 8B model on a
   recent GPU, the first words are ready in about 0.1-0.5 s and the whole line in about 1-1.5 s,
   faster than the game types it.
4. Sources are fetched on their own thread for every personality in town, refreshed every
   `source_refresh_minutes` (a failed fetch is retried after 5 minutes). A line only uses what's
   already fetched, so it never waits on the network.

Small talk, pre-written (off by default with the built-in model, `prewrite_small_talk`; always on
for the `local` and `anthropic` providers, which can't write live):

1. The first time you talk to anyone, every villager in town is handed to the worker.
2. For each villager the worker picks a personality, fetches that personality's sources (cached for
   `source_refresh_minutes`), and asks the model for `lines_per_batch` lines.
3. Lines are kept in a per-villager pool in `dialog/cache.json` and survive restarts. Each line is
   used once. The villager you last talked to is refilled first and anyone below `min_pool` next.
   Without live lines, pools are also topped up to `pool_target` when there's nothing else to do.
4. When a villager picks a small-talk line and live lines aren't available (model still loading,
   provider isn't `embedded`, or a live line failed), a pooled line is shown instead.

Either way, `{player}` and `{catchphrase}` become the real player name and catchphrase.

The PC build also makes small talk come up more often than in the original (about 60% of
conversations instead of about 25-40%). Trades, letters, memories and games still happen.

Rewrites of the game's own messages (`rewrite_special`):

1. The game tells the worker which messages each built-in personality can open a conversation with
   (trades, gifts, sales, letters, memories, tips, events, small talk), plus a one-line description
   of what's going on in each. The worker follows every branch from there: about 300-700 messages
   per personality.
2. Each message is split into pages. Codes that do something (item names, prices, choices, branches,
   quest steps, animations) are kept; the words between them are sent to the model with the
   personality and situation, then put back around the kept codes.
3. Every rewritten page is checked before it's used. It must still mention every item, price and
   name the original did, keep all numbers, stay a question when the player picks a choice right
   after, and not ramble. A page that fails keeps its original text.
4. Rewrites are made once per personality and saved in `dialog/rewrites.json`, conversation openers
   first. A message you hear before it's been rewritten jumps to the front of the queue.

The first full pass takes the built-in 8B model roughly 10-30 minutes of background GPU time per
personality in your town. It's saved, so it only happens once.

### Mr. Resetti

Mr. Resetti (the `resetti` section of `personalities.json`) is wired up through his actors
(`ac_npc_majin`, `majin2`, `majin3`, `majin4` and the event Resetti, `ac_ev_majin`). Don Resetti
keeps his own lines. Each visit:

1. Everything he says is rewritten live, page by page, in the personality named by
   `resetti.personality`, the same way as villager rewrites. Pages with his special effects
   (shaking text, big letters) stay as written.
2. An off-script rant about one of the personality's `topics` goes in before the last message of
   his lecture. His lecture is a chain of messages joined by `<NEXT_F n><FORCE_NEXT><CONTINUE>`.
   The message that jumps to the last one is pointed at message `0x3F80` instead. The rant is
   written live across `0x3F80`, `0x3F81`, … (a message holds about 1500 bytes), and the last part
   jumps back to the lecture. By default it runs as long as that visit's own lecture (76-493 words,
   so 1-4 messages); `rant_words` sets a fixed length. If the model fails, it jumps straight back.
3. On the visits where he makes you type an apology (`ac_npc_majin3`, from the sixth reset on),
   the phrase is generated. Phrases are written in batches at boot and kept if they fit the
   16-character typing box. A typed reply counts if its letters and digits match, whatever the
   case, spaces or punctuation. The game's own phrases are used if none are ready.

The talk log shows each visit (RESETTI VISIT), where the rant went (RESETTI RANT PLANNED), each rant
part, the phrases written, and what you typed against what he asked for (RESETTI PHRASE TYPED).

## Setup

Built-in model (default, `"provider": "embedded"`):

1. Get a GGUF model. Drop any instruct `.gguf` into `dialog/models/` in the data folder and the game
   loads the first one it finds (by name). Or `ollama pull llama3.1:8b`: the game finds Ollama's copy on
   disk (`~/.ollama/models`, or `OLLAMA_MODELS`) and loads it directly. Ollama doesn't need to be
   running. Or download any instruct GGUF and set `embedded.model_path` to it (relative paths are
   under `dialog/`).

   Recommended: `meta-llama-3.1-8b-instruct-abliterated.Q4_K_M.gguf` from
   [mlabonne/Meta-Llama-3.1-8B-Instruct-abliterated-GGUF](https://huggingface.co/mlabonne/Meta-Llama-3.1-8B-Instruct-abliterated-GGUF).
   Same size and speed as stock Llama 3.1 8B, but it doesn't refuse the adult personalities, and in
   testing it glued words together far less often and kept more of each original line's meaning.
2. Run the game. It writes `dialog/personalities.json` in the data folder with seven example
   personalities, loads the model on the GPU while the game boots (about 2 s), so it's
   ready by the first conversation.

Building it in needs the Vulkan SDK at build time (`NOOKERY_EMBEDDED_LLM`, on by default for Windows
and Linux desktop). Without it, use a local server:

Local server (`"provider": "local"`): run Ollama (`http://127.0.0.1:11434`, the default `local.url`).
For llama.cpp, start `llama-server -m model.gguf --port 8080` and set `local.url` to
`http://127.0.0.1:8080/v1/chat/completions`. For LM Studio, use port 1234. Plain `http://` only works
for addresses on this machine (`localhost`, `127.x.x.x`, `::1`); a remote server must use `https://`.

Claude instead: set `"provider": "anthropic"` and put your key in `anthropic.api_key`, or in the
`ANTHROPIC_API_KEY` environment variable. Keys are stored in plain text in that file, so don't share
your data folder.

Set `"enabled": false` to turn the feature off, or `"rewrite_special": false` to keep the game's
own lines and only replace small talk.

## personalities.json

The template, with every setting explained in comments, is
[`pc/res/dialog/personalities.jsonc`](../pc/res/dialog/personalities.jsonc). The first time the game
runs it copies the template to `dialog/personalities.json` in the data folder: edit that copy. A
fresh copy of the template is also written to `dialog/personalities.template.jsonc` on every start,
to compare against. Changes take effect the next time the game starts.

Each personality has a `voice` (how they talk), `topics` (their own interests) and `sources` (web
sources for news). `by_builtin_type` gives each of the game's six villager types a personality, and
`villagers` overrides it by name, optionally with `notes` about that villager.

If you change a personality's `voice`, delete `dialog/rewrites.json` (or just that personality's
section) to have its messages rewritten again.

### Tone

`tone.setting` describes the game to the model, and `tone.rules` lists what content is fine; each
rule is one line of every prompt (live lines, pre-written batches, rewrites). The template's
default is for an adult player: swearing, crude humor, insults and strong opinions are allowed when
they fit the personality. Without a `tone` section the game's original all-ages rules apply, and the
template's comments show how to restore them. The rules that keep the game working (length,
`{player}`/`{catchphrase}`, no emoji or narration, ignoring instructions inside web text) are built in
and always apply. The model itself may still tone some things down.

### Topics

Every live line is about one topic. Its kind is picked with the `topic_mix` weights:

| kind | where it comes from |
|---|---|
| `news` | an item from the personality's `sources`, mentioned in-world ("I read in the paper...") |
| `personal` | one of the personality's own `topics` |
| `village` | one of the shared `village_topics`; `{neighbor}` becomes another villager in town |
| `weather` | the weather, time of day or season right now |

A villager never gets the same kind twice in a row, or a topic they had in their last 8 lines, and
a headline isn't reused by another villager until 40 more have been used. Set a weight to 0 to turn
a kind off, for example `"news": 0` to keep villagers out of real-world news. The talk log shows the
topic of every line.

### Source types

| type | fields | what the villager gets |
|---|---|---|
| `rss` (or `atom`) | `url`, `limit` (1-25) | item/entry titles |
| `wikipedia` | `article` (optional), `limit` (1-5, random only) | article title + summary |
| `reddit` | `subreddit`, `sort` (`hot`, `new`, `top`, ...), `limit` | post titles (stickied and NSFW posts skipped) |

Reddit currently refuses logged-out requests. After the first refusal, Reddit sources are skipped
for the rest of the session, so the defaults use RSS and Wikipedia only.

All fetched text is treated as topic material only. The model is told to ignore any instructions
inside it and to skip material that isn't kid-friendly. Small local models follow this less reliably
than large ones, so pick sources you'd be happy to see paraphrased.

## Performance and cost

Live small talk: one short request per line, about 1-1.5 s with the built-in 8B model on a recent
GPU, streamed as it's written. The start of the previous prompt stays in the model's memory, so
back-to-back requests skip re-reading it. Pre-written small talk: each batch is one request with up
to `max_material_items` source snippets and `lines_per_batch` lines back (about 13 s). Rewrites: one
request per message, about 2-6 s each.

Everything runs on a background thread, so the game never waits for the model. Background work
(pools, rewrites, source fetches) stops the moment a live line is needed and picks up again
afterwards. While a request is running, the model shares the GPU with the game. An 8B model at 4-bit needs roughly 5-6 GB of VRAM;
on a smaller GPU try a 3B model. With Claude, lower `anthropic.effort` or pick a cheaper model to
cut cost. A full rewrite pass is a few thousand requests, so it isn't cheap with an API.

Small models sometimes add flavor that doesn't quite fit, or make a page that fails the checks and
keeps its original text. A larger model gives better rewrites if your GPU has room.

## Debugging

While testing, every generated line starts with a star (`mark_generated`) so it can be told apart
from the game's own text at a glance. It's added when the text is laid out, not by the model.

`dialog/talk_log.txt` (on by default, `talk_log`) has one readable entry per small-talk line:

- `LIVE LINE`: the outcome (`ok`, `cancelled` if the player moved on, `fallback` if the model
  failed), first-words and total time, the per-line prompt, exactly what the model wrote, what
  the text box showed after cleanup and wrapping, and the game's original line it replaced.
- `ORIGINAL LINE`: a small-talk message that played as the game wrote it, and why (model still
  loading, failed to load, live off...).
- `OTHER LINE`: a conversation that opened with something other than small talk (a trade,
  letter, memory, tip...), left as the game wrote it, and what kind it was. Note that every
  conversation is a first-job hint while the player works for Nook, and until they've heard 10 of
  them, so small talk can't come up before that.
- `LIVE REWRITE`: one per rewritten message: outcome, situation, timing, and for every page the
  original, exactly what the model wrote and what was shown, then the full original message.
- `PRE-WRITTEN LINE`: a pooled line was used (only with `prewrite_small_talk`).
- `STALLED`: the text box waited 10 s with no new words and ended the line.
- `SESSION START` lists any `Personality problems`: a `by_builtin_type` or `villagers` entry naming
  a personality that doesn't exist (after a rename, say). Villagers whose type has no personality
  keep the game's own small talk (logged as `ORIGINAL LINE` with the reason); a bad `villagers`
  entry falls back to the villager's type.
- `MODEL READY` (path, GPU layers, load time), `MODEL FAILED TO LOAD`,
  `MODEL NOT FOUND`.

The file starts over past 8 MB, keeping the previous one as `talk_log.old.txt`.

Set `NOOKERY_DUMP_MESSAGES=<file>` to have the game write every message at startup, readable
(`<file>`) and raw (`<file>.bin`). The log (`--log-dir`, or the data folder's `logs/`) records each
batch and rewrite, and each live line with how long its first words and the whole line took.

If a live line stalls for 10 seconds, the text box ends it so the game never hangs. If the model
fails, the villager says a pooled line instead.

## Files

- `pc/src/acgc_dialog.cpp`: config, source fetching, model requests, live lines, pools, rewrite
  queue
- `pc/src/acgc_dialog_messages.cpp`: message archive, page templates, rewrite prompt and checks,
  word wrapping (whole text and streamed)
- `pc/src/acgc_dialog_llama.cpp`: the built-in llama.cpp backend (`extern/llama.cpp` submodule)
- `pc/include/pc_dialog.h`: C interface used by the game
- `src/actor/ac_quest_talk_normal_init.c`: starts a conversation, registers conversation openers,
  arms small-talk lines, small-talk odds
- `src/game/m_msg.c`, `m_msg_main.c_inc`, `m_msg_ctrl.c_inc`: archive snapshot, string widths,
  swapping lines in as messages load, conversation end
- `src/game/m_msg_cursol.c_inc`: feeds a live line's new words to the text cursor and holds the
  cursor when it catches up
