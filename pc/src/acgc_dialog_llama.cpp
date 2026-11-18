#include "acgc_dialog_llama.hpp"

#include "borealis/log.hpp"

#include <llama.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <vector>

constexpr borealis::Log Log{"acgc::dialog::llm"};

namespace acgc::dialog::llm {
namespace {

struct State {
    std::filesystem::path path;
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    llama_batch_ext* batch = nullptr;
    int contextTokens = 0;
    std::vector<llama_token> cached; // tokens currently in the context's memory, in order
};

State s_state;

void log_callback(ggml_log_level level, const char* text, void*) {
    // GGML_LOG_LEVEL_CONT continues the previous line (e.g. model-loading progress dots)
    // and is numbered above ERROR, so it has to be filtered by name.
    if (level < GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_CONT || text == nullptr) {
        return;
    }
    std::string msg(text);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }
    if (msg.empty()) {
        return;
    }
    if (level >= GGML_LOG_LEVEL_ERROR) {
        Log.error("{}", msg);
    } else {
        Log.warn("{}", msg);
    }
}

void init_backend_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(log_callback, nullptr);
        llama_backend_init();
    });
}

// Fills the batch with tokens at consecutive positions starting at pos0; logits only for the last.
void batch_set_tokens(llama_batch_ext* batch, const llama_token* tokens, int32_t count, llama_pos pos0) {
    llama_batch_ext_clear(batch);
    for (int32_t i = 0; i < count; ++i) {
        const int32_t idx = llama_batch_ext_add_token(batch, 0, tokens[i]);
        const llama_pos pos = pos0 + i;
        llama_batch_ext_set_pos(batch, idx, &pos);
    }
    llama_batch_ext_set_output_logits(batch, count - 1, true);
}

std::string apply_chat_template(const std::string& system, const std::string& user) {
    const llama_chat_message messages[] = {
        { "system", system.c_str() },
        { "user", user.c_str() },
    };
    const char* tmpl = llama_model_chat_template(s_state.model, nullptr);
    std::vector<char> buf(system.size() + user.size() + 1024);
    int n = llama_chat_apply_template(tmpl, messages, 2, true, buf.data(), static_cast<int32_t>(buf.size()));
    if (n > static_cast<int>(buf.size())) {
        buf.resize(static_cast<size_t>(n));
        n = llama_chat_apply_template(tmpl, messages, 2, true, buf.data(), static_cast<int32_t>(buf.size()));
    }
    if (n < 0) {
        // Model has no template llama.cpp recognizes; a plain layout still works for most instruct models.
        return "System: " + system + "\n\nUser: " + user + "\n\nAssistant: ";
    }
    return std::string(buf.data(), static_cast<size_t>(n));
}

} // namespace

bool load(const Options& options, std::string& error) {
    if (s_state.model != nullptr && s_state.path == options.model) {
        return true;
    }
    unload();
    init_backend_once();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(options.model, ec)) {
        error = "model file not found: " + options.model.string();
        return false;
    }

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = options.gpuLayers;
    const std::u8string u8 = options.model.u8string();
    const std::string pathUtf8(u8.begin(), u8.end());
    s_state.model = llama_model_load_from_file(pathUtf8.c_str(), mp);
    if (s_state.model == nullptr) {
        error = "llama.cpp could not load " + options.model.string();
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = static_cast<uint32_t>(options.contextTokens);
    cp.n_batch = static_cast<uint32_t>(options.contextTokens);
    cp.n_threads = options.threads;
    cp.n_threads_batch = options.threads;
    s_state.ctx = llama_init_from_model(s_state.model, cp);
    if (s_state.ctx == nullptr) {
        error = "llama.cpp could not create a context";
        unload();
        return false;
    }
    s_state.batch = llama_batch_ext_init(s_state.ctx);
    s_state.path = options.model;
    s_state.contextTokens = options.contextTokens;
    Log.info("Loaded {} ({} GPU layers requested, {}-token context)", options.model.string(), options.gpuLayers,
             options.contextTokens);
    return true;
}

std::optional<std::string> generate(const std::string& system, const std::string& user, const std::string& grammar,
                                    int maxTokens, float temperature, const StopFn& stop, std::string& error,
                                    const PieceFn& onPiece) {
    if (s_state.ctx == nullptr) {
        error = "model not loaded";
        return std::nullopt;
    }
    const llama_vocab* vocab = llama_model_get_vocab(s_state.model);
    auto shouldStop = [&] { return stop && stop(); };

    const std::string prompt = apply_chat_template(system, user);
    const int needed = -llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), nullptr, 0, true, true);
    std::vector<llama_token> tokens(static_cast<size_t>(std::max(needed, 0)));
    if (needed <= 0 || llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(),
                                      static_cast<int32_t>(tokens.size()), true, true) < 0) {
        error = "could not tokenize prompt";
        return std::nullopt;
    }
    if (static_cast<int>(tokens.size()) + maxTokens > s_state.contextTokens) {
        error = "prompt (" + std::to_string(tokens.size()) + " tokens) too long for the " +
                std::to_string(s_state.contextTokens) + "-token context; lower max_material_items or raise context";
        return std::nullopt;
    }

    llama_sampler* chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler* grammarSampler = llama_sampler_init_grammar(vocab, grammar.c_str(), "root");
    if (grammarSampler == nullptr) {
        llama_sampler_free(chain);
        error = "invalid output grammar";
        return std::nullopt;
    }
    llama_sampler_chain_add(chain, grammarSampler);
    // Discourages the loops a constrained small model falls into when it runs out of things to say.
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab), 64, 1.1f, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    // Reuse whatever the last prompt had in common with this one (usually the system
    // prompt); at least one token has to be processed to get fresh logits.
    llama_memory_t memory = llama_get_memory(s_state.ctx);
    size_t common = 0;
    while (common < s_state.cached.size() && common + 1 < tokens.size() && s_state.cached[common] == tokens[common]) {
        ++common;
    }
    if (common == 0 || !llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(common), -1)) {
        llama_memory_clear(memory, true);
        common = 0;
    }
    s_state.cached.assign(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(common));

    std::string out;
    const llama_token* next = tokens.data() + common;
    int32_t count = static_cast<int32_t>(tokens.size() - common);
    llama_pos pos = static_cast<llama_pos>(common);
    llama_token tok = 0;
    bool finished = false;
    for (int generated = 0; generated < maxTokens; ++generated) {
        if (shouldStop()) {
            break;
        }
        batch_set_tokens(s_state.batch, next, count, pos);
        if (const int ret = llama_process(s_state.ctx, LLAMA_PROCESS_TYPE_DECODE, s_state.batch); ret != 0) {
            error = "llama_process failed (" + std::to_string(ret) + ")";
            s_state.cached.clear();
            llama_memory_clear(memory, true);
            break;
        }
        s_state.cached.insert(s_state.cached.end(), next, next + count);
        pos += count;
        tok = llama_sampler_sample(chain, s_state.ctx, -1);
        if (llama_vocab_is_eog(vocab, tok)) {
            finished = true;
            break;
        }
        char piece[256];
        const int n = llama_token_to_piece(vocab, tok, piece, sizeof(piece), 0, false);
        if (n > 0) {
            out.append(piece, static_cast<size_t>(n));
            if (onPiece) {
                onPiece(std::string_view(piece, static_cast<size_t>(n)));
            }
        }
        next = &tok;
        count = 1;
    }
    llama_sampler_free(chain);

    if (shouldStop()) {
        return std::nullopt;
    }
    if (!finished && error.empty()) {
        // Hit maxTokens mid-JSON: keep what parses; the caller salvages complete strings.
        Log.warn("Generation stopped at {} tokens before the model finished", maxTokens);
    }
    if (out.empty()) {
        if (error.empty()) {
            error = "model produced no text";
        }
        return std::nullopt;
    }
    return out;
}

void unload() {
    if (s_state.batch != nullptr) {
        llama_batch_ext_free(s_state.batch);
    }
    if (s_state.ctx != nullptr) {
        llama_free(s_state.ctx);
    }
    if (s_state.model != nullptr) {
        llama_model_free(s_state.model);
    }
    s_state = {};
}

std::optional<std::filesystem::path> find_ollama_model(const std::string& name) {
    std::string model = name;
    std::string tag = "latest";
    if (const size_t colon = model.rfind(':'); colon != std::string::npos) {
        tag = model.substr(colon + 1);
        model.resize(colon);
    }
    if (model.find('/') == std::string::npos) {
        model = "library/" + model;
    }

    std::vector<std::filesystem::path> roots;
    if (const char* env = std::getenv("OLLAMA_MODELS"); env != nullptr && *env != '\0') {
        roots.emplace_back(env);
    }
#if defined(_WIN32)
    if (const char* home = std::getenv("USERPROFILE")) {
        roots.emplace_back(std::filesystem::path(home) / ".ollama" / "models");
    }
#else
    if (const char* home = std::getenv("HOME")) {
        roots.emplace_back(std::filesystem::path(home) / ".ollama" / "models");
    }
    roots.emplace_back("/usr/share/ollama/.ollama/models"); // Linux system service install
#endif

    for (const auto& root : roots) {
        std::ifstream in(root / "manifests" / "registry.ollama.ai" / model / tag);
        if (!in) {
            continue;
        }
        const nlohmann::json manifest = nlohmann::json::parse(in, nullptr, false);
        if (manifest.is_discarded() || !manifest.contains("layers")) {
            continue;
        }
        for (const auto& layer : manifest["layers"]) {
            if (layer.value("mediaType", "") != "application/vnd.ollama.image.model") {
                continue;
            }
            std::string digest = layer.value("digest", "");
            if (const size_t c = digest.find(':'); c != std::string::npos) {
                digest[c] = '-'; // blobs are stored as sha256-<hex>
            }
            const auto blob = root / "blobs" / digest;
            std::error_code ec;
            if (std::filesystem::is_regular_file(blob, ec)) {
                return blob;
            }
        }
    }
    return std::nullopt;
}

} // namespace acgc::dialog::llm
