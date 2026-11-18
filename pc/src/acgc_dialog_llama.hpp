#pragma once

// In-process llama.cpp backend for generated villager dialog. Only built when
// NOOKERY_EMBEDDED_LLM is on. All functions run on the dialog worker thread.

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace acgc::dialog::llm {

struct Options {
    std::filesystem::path model; // GGUF file
    int gpuLayers = -1;          // -1 = all layers on the GPU
    int contextTokens = 4096;
    int threads = 4;             // CPU threads for anything not offloaded
};

// Loads the model if it isn't loaded already (or a different file was requested).
bool load(const Options& options, std::string& error);

// Polled between tokens; returning true abandons the generation.
using StopFn = std::function<bool()>;
// Receives each piece of text as it's generated.
using PieceFn = std::function<void(std::string_view)>;

// One-shot chat completion. `grammar` is GBNF; the output is constrained to it.
// Returns std::nullopt on failure or when `stop` returns true. The start of the
// previous prompt stays cached, so prompts sharing a prefix start faster.
std::optional<std::string> generate(const std::string& system, const std::string& user, const std::string& grammar,
                                    int maxTokens, float temperature, const StopFn& stop, std::string& error,
                                    const PieceFn& onPiece = {});

void unload();

// Finds the GGUF blob Ollama stored for `name` (e.g. "llama3.1:8b"), so a model already
// pulled with Ollama can be reused without Ollama running.
std::optional<std::filesystem::path> find_ollama_model(const std::string& name);

} // namespace acgc::dialog::llm
