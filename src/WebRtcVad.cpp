#include "WebRtcVad.h"

#include <QLibrary>

#include <QStringList>

namespace {

struct RuntimeSymbols {
    using NewFn = void *(*)();
    using FreeFn = void (*)(void *);
    using SetModeFn = int (*)(void *, int);
    using SetSampleRateFn = int (*)(void *, int);
    using ProcessFn = int (*)(void *, const int16_t *, std::size_t);

    NewFn fvadNew = nullptr;
    FreeFn fvadFree = nullptr;
    SetModeFn fvadSetMode = nullptr;
    SetSampleRateFn fvadSetSampleRate = nullptr;
    ProcessFn fvadProcess = nullptr;
};

} // namespace

class WebRtcVad::Impl {
public:
    QLibrary library;
    RuntimeSymbols symbols;
    void *instance = nullptr;
    bool runtimeLoaded = false;
};

WebRtcVad::WebRtcVad()
    : impl_(new Impl())
{
}

WebRtcVad::~WebRtcVad()
{
    releaseState();
    delete impl_;
}

bool WebRtcVad::isRuntimeAvailable(QString *errorMessage)
{
    WebRtcVad probe;
    QString initializationError;
    if (probe.initialize(2, &initializationError)) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = initializationError;
    }
    return false;
}

bool WebRtcVad::initialize(int aggressiveness, QString *errorMessage)
{
    if (aggressiveness < 0 || aggressiveness > 3) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("VAD aggressiveness must be between 0 and 3.");
        }
        return false;
    }

    if (!ensureRuntimeLoaded(errorMessage)) {
        return false;
    }

    releaseState();
    impl_->instance = impl_->symbols.fvadNew();
    if (impl_->instance == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create WebRTC VAD instance.");
        }
        return false;
    }

    if (impl_->symbols.fvadSetSampleRate(impl_->instance, 16000) != 0) {
        releaseState();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to set WebRTC VAD sample rate to 16 kHz.");
        }
        return false;
    }

    if (impl_->symbols.fvadSetMode(impl_->instance, aggressiveness) != 0) {
        releaseState();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to set WebRTC VAD aggressiveness.");
        }
        return false;
    }

    return true;
}

std::optional<bool> WebRtcVad::processFrame(const int16_t *samples, std::size_t sampleCount, QString *errorMessage)
{
    if (samples == nullptr || sampleCount == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid VAD frame.");
        }
        return std::nullopt;
    }
    if (impl_->instance == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("WebRTC VAD is not initialized.");
        }
        return std::nullopt;
    }

    const int result = impl_->symbols.fvadProcess(impl_->instance, samples, sampleCount);
    if (result < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("WebRTC VAD failed to process audio frame.");
        }
        return std::nullopt;
    }
    return result == 1;
}

bool WebRtcVad::ensureRuntimeLoaded(QString *errorMessage)
{
    if (impl_->runtimeLoaded) {
        return true;
    }

    const QStringList libraryCandidates = {
            QStringLiteral("fvad"),
            QStringLiteral("libfvad.so.0"),
            QStringLiteral("libfvad.so"),
    };

    QString lastError;
    for (const QString &candidate : libraryCandidates) {
        impl_->library.setFileName(candidate);
        if (impl_->library.load()) {
            break;
        }
        lastError = impl_->library.errorString();
    }

    if (!impl_->library.isLoaded()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("WebRTC VAD runtime (libfvad) is unavailable: %1").arg(lastError);
        }
        return false;
    }

    impl_->symbols.fvadNew = reinterpret_cast<RuntimeSymbols::NewFn>(impl_->library.resolve("fvad_new"));
    impl_->symbols.fvadFree = reinterpret_cast<RuntimeSymbols::FreeFn>(impl_->library.resolve("fvad_free"));
    impl_->symbols.fvadSetMode = reinterpret_cast<RuntimeSymbols::SetModeFn>(impl_->library.resolve("fvad_set_mode"));
    impl_->symbols.fvadSetSampleRate = reinterpret_cast<RuntimeSymbols::SetSampleRateFn>(
            impl_->library.resolve("fvad_set_sample_rate"));
    impl_->symbols.fvadProcess = reinterpret_cast<RuntimeSymbols::ProcessFn>(impl_->library.resolve("fvad_process"));

    if (impl_->symbols.fvadNew == nullptr || impl_->symbols.fvadFree == nullptr || impl_->symbols.fvadSetMode == nullptr
            || impl_->symbols.fvadSetSampleRate == nullptr || impl_->symbols.fvadProcess == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("WebRTC VAD runtime is missing required symbols.");
        }
        impl_->library.unload();
        return false;
    }

    impl_->runtimeLoaded = true;
    return true;
}

void WebRtcVad::releaseState()
{
    if (impl_->instance == nullptr) {
        return;
    }
    if (impl_->symbols.fvadFree != nullptr) {
        impl_->symbols.fvadFree(impl_->instance);
    }
    impl_->instance = nullptr;
}
