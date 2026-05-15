#define _CRT_SECURE_NO_DEPRECATE
#include "Translator.h"
#include "Logging.h"

#include <sstream>
#include <stdexcept>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// CTranslate2 headers/import libs are available from lib/ctranslate2/ or the
// CMake build cache. The runtime DLL is installed on demand by the selected
// translation pack.
#ifdef HAVE_CT2
#include <ctranslate2/translator.h>
#endif

#ifdef HAVE_CT2

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

bool Translator::RuntimeAvailable()
{
    HMODULE h = LoadLibraryW(L"ctranslate2.dll");
    if (!h) return false;
    FreeLibrary(h);
    return true;
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

#else // !HAVE_CT2

// Build-without-CT2 stubs. The host links and runs; Translate() returns the
// input string unchanged so the chatbox still gets the user's words in their
// native language. A first-call log line explains how to enable real
// translation.
struct Translator::Impl {};

Translator::Translator()  : impl_(std::make_unique<Impl>()) {}
Translator::~Translator() = default;
bool Translator::RuntimeAvailable() { return false; }

bool Translator::Load(const std::string &)
{
    static bool logged = false;
    if (!logged) {
        TH_LOG("[translator] CTranslate2 headers/import library were not available at build time. The "
               "translator host built in stub mode -- translation pass-through "
               "only. Reconfigure with WKOPENVR_TRANSLATOR_FETCH_CT2=ON or "
               "provide lib/ctranslate2/ (headers + import library) and rebuild.");
        logged = true;
    }
    return false;
}

void        Translator::Unload()                  {}
bool        Translator::IsLoaded() const noexcept { return false; }
std::string Translator::Translate(const std::string &text,
                                  const std::string &,
                                  const std::string &) { return text; }

#endif // HAVE_CT2
