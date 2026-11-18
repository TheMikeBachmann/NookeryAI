# Generated villager dialog

Villagers talk in a voice you define. Two things change:

- **Small talk** is written live by a language model the moment you talk to a villager, and the
  words appear in the text box as they're generated. Each line can use the current weather, time
  and mood, and topics from web sources you choose for each personality (RSS/Atom feeds,
  Wikipedia, Reddit). Lines never run out or repeat.
- **Everything else a town villager says**, such as trade offers, sales, gifts, letters, memories
  and tips, is rewritten in their personality's voice. The game's mechanics are untouched: items,
  prices, names, yes/no choices and what happens next all stay exactly as they were. Only the
  wording changes.

By default the model runs inside the game: llama.cpp is built in and runs on the GPU through
Vulkan, so no separate server is needed. A local server (Ollama, llama.cpp `llama-server`,
LM Studio) or the Claude API can be used instead.

Island villagers keep their original text. Anything not ready yet (first launch, no model, no
network) plays the original line.

## How it works

Small talk, live (`live_small_talk`, built-in model only):

1. When the game starts, a background worker loads the model onto the GPU and warms it up.
2. When a villager picks a small-talk line, the worker drops whatever background job it was doing
   and writes one line for that villager from their personality, mood, the game's current weather,
   time and date, a topic from the personality's sources (or everyday village life), and their last
   few lines so they don't repeat themselves.
3. The words are word-wrapped and paged as they're generated and handed to the message window. If
   the window's text cursor catches up with the model, it waits a frame. With an 8B model on a
   recent GPU, the first words are ready in about 0.1-0.5 s and the whole line in about 1-1.5 s,
   faster than the game types it.
4. Sources are only fetched in the background, never while you wait.

Small talk, pre-written (the fallback, and what other providers use):

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

## Setup

Built-in model (default, `"provider": "embedded"`):

1. Get a GGUF model. The easiest way is `ollama pull llama3.1:8b`: the game finds Ollama's copy on
   disk (`~/.ollama/models`, or `OLLAMA_MODELS`) and loads it directly. Ollama doesn't need to be
   running. Or download any instruct GGUF and set `embedded.model_path` to it (relative paths are
   under `dialog/`).
2. Run the game. It writes `dialog/personalities.json` in the data folder with seven example
   personalities, loads the model on the GPU the first time it needs it, and starts working as
   soon as you talk to someone.

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

```jsonc
{
  "enabled": true,
  "provider": "embedded",        // "embedded", "local" or "anthropic"
  "embedded": {
    "model_path": "",            // .gguf file; empty = reuse Ollama's copy of ollama_model
    "ollama_model": "llama3.1:8b",
    "gpu_layers": -1,            // -1 = whole model on the GPU; lower to share VRAM
    "context": 4096,             // tokens of prompt + reply
    "threads": 4                 // CPU threads for anything not on the GPU
  },
  "local": {
    "url": "http://127.0.0.1:11434/v1/chat/completions",
    "model": "llama3.1:8b",      // any model name your server knows
    "api_key": "",               // only if your server requires one
    "timeout_seconds": 600       // CPU-only generation can be slow
  },
  "anthropic": { "api_key": "", "model": "claude-opus-5-5", "effort": "medium" },
  "max_material_items": 10,      // source snippets per prompt; keep low for small context windows
  "lines_per_batch": 12,
  "min_pool": 8,                 // refill a villager urgently below this many lines
  "pool_target": 24,             // keep topping up to this many when idle
  "rewrite_special": true,       // rewrite trades, letters, memories... in each personality's voice
  "live_small_talk": true,       // write small talk as you talk (embedded provider only)
  "source_refresh_minutes": 60,

  "personalities": {
    "stargazer": {
      "voice": "Wide-eyed space nerd who stays up all night with a telescope...",
      "sources": [
        { "type": "rss", "url": "https://feeds.bbci.co.uk/news/science_and_environment/rss.xml", "limit": 8 },
        { "type": "wikipedia", "article": "Jupiter" },   // one specific article
        { "type": "wikipedia", "limit": 3 }              // no article = random articles
      ]
    }
  },

  // Fallback personality for each of the game's six built-in types.
  "by_builtin_type": {
    "normal": "bookworm", "peppy": "sunshine", "lazy": "gourmand",
    "jock": "gym_rat", "cranky": "curmudgeon", "snooty": "aesthete"
  },

  // Per-villager overrides, by in-game name. A string, or an object with extra notes.
  "villagers": {
    "Bob": "stargazer",
    "Goose": { "personality": "gym_rat", "notes": "Training for the town marathon; hates losing to Bob." }
  }
}
```

Comments are allowed in this file. If you change a personality's `voice`, delete
`dialog/rewrites.json` (or just that personality's section) to have its messages rewritten again.

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
