#define _CRT_SECURE_NO_DEPRECATE
#include "Translator.h"
#include "Logging.h"

// CTranslate2 C++ API. The library is vendored under lib/ctranslate2/.
// CMakeLists links ctranslate2.lib and stages ctranslate2.dll.
#include <ctranslate2/translator.h>

#include <sstream>
#include <stdexcept>
#include <vector>

struct Translator::Impl
{
    std::unique_ptr<ctranslate2::Translator> ct2;
    std::string model_dir;
};

Translator::Translator()
    : impl_(std::make_unique<Impl>())
{}

Translator::~Translator()
{
    Unload();
}

bool Translator::Load(const std::string &model_dir)
{
    Unload();
    try {
        ctranslate2::Device device = ctranslate2::Device::CPU;
        ctranslate2::ComputeType compute = ctranslate2::ComputeType::INT8;
        impl_->ct2 = std::make_unique<ctranslate2::Translator>(
            model_dir, device, compute);
        impl_->model_dir = model_dir;
        TH_LOG("[translator] model loaded from '%s'", model_dir.c_str());
        return true;
    } catch (const std::exception &e) {
        TH_LOG("[translator] load failed: %s", e.what());
        return false;
    }
}

void Translator::Unload()
{
    impl_->ct2.reset();
    impl_->model_dir.clear();
}

bool Translator::IsLoaded() const noexcept
{
    return impl_->ct2 != nullptr;
}

std::string Translator::Translate(const std::string &text,
                                  const std::string &src_lang,
                                  const std::string &tgt_lang)
{
    if (text.empty()) return {};
    if (src_lang == tgt_lang) return text;
    if (!impl_->ct2) return text;

    try {
        // OPUS-MT tokenization: prepend target language token, then space-split.
        // The CTranslate2 OPUS-MT format expects tokens like ">>de<< Hello world".
        std::string prefix = ">>" + tgt_lang + "<< ";
        std::string src_text = prefix + text;

        // Split on whitespace -- OPUS-MT uses Moses tokenization in practice;
        // for v1 simple whitespace split is sufficient for common cases.
        std::vector<std::string> tokens;
        std::istringstream ss(src_text);
        std::string tok;
        while (ss >> tok) tokens.push_back(tok);

        ctranslate2::TranslationOptions opts;
        opts.beam_size = 2;
        opts.max_decoding_length = 256;

        std::vector<std::vector<std::string>> batch = { tokens };
        auto results = impl_->ct2->translate_batch(batch, opts);

        if (results.empty() || results[0].hypotheses.empty()) return text;

        // Detokenize: join with spaces and strip >>tgt<< prefix if present.
        std::string out;
        for (const auto &t : results[0].hypotheses[0]) {
            if (!out.empty()) out += ' ';
            out += t;
        }
        return out;
    } catch (const std::exception &e) {
        TH_LOG("[translator] inference error: %s", e.what());
        return text;
    }
}
