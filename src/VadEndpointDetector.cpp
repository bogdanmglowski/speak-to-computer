#include "VadEndpointDetector.h"

#include "WebRtcVad.h"

#include <QtEndian>

#include <array>

namespace {

constexpr int kFrameMs = 20;
constexpr std::size_t kFrameSampleCount = 320;
constexpr std::size_t kFrameByteSize = kFrameSampleCount * sizeof(int16_t);

class WebRtcVadFrameClassifier final : public VadFrameClassifier {
public:
    bool initialize(int aggressiveness, QString *errorMessage) override
    {
        return vad_.initialize(aggressiveness, errorMessage);
    }

    std::optional<bool> processFrame(const int16_t *samples, std::size_t sampleCount, QString *errorMessage) override
    {
        return vad_.processFrame(samples, sampleCount, errorMessage);
    }

private:
    WebRtcVad vad_;
};

} // namespace

VadEndpointDetector::VadEndpointDetector()
    : VadEndpointDetector(std::make_unique<WebRtcVadFrameClassifier>())
{
}

VadEndpointDetector::VadEndpointDetector(std::unique_ptr<VadFrameClassifier> classifier)
    : classifier_(std::move(classifier))
{
}

bool VadEndpointDetector::reset(const VadEndpointConfig &config, QString *errorMessage)
{
    if (!classifier_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("VAD classifier is unavailable.");
        }
        clear();
        return false;
    }

    config_ = sanitizeConfig(config);
    pendingPcm_.clear();
    autoStop_ = false;
    hasQualifiedSpeech_ = false;
    contiguousSpeechMs_ = 0;
    trailingSilenceMs_ = 0;

    if (!classifier_->initialize(config_.aggressiveness, errorMessage)) {
        armed_ = false;
        return false;
    }

    armed_ = true;
    return true;
}

void VadEndpointDetector::clear()
{
    armed_ = false;
    autoStop_ = false;
    hasQualifiedSpeech_ = false;
    contiguousSpeechMs_ = 0;
    trailingSilenceMs_ = 0;
    pendingPcm_.clear();
}

bool VadEndpointDetector::consumePcmChunk(const QByteArray &chunk, QString *errorMessage)
{
    if (chunk.isEmpty() || !armed_ || autoStop_) {
        return true;
    }

    pendingPcm_.append(chunk);

    std::array<int16_t, kFrameSampleCount> frameSamples;
    int processedBytes = 0;
    while (pendingPcm_.size() - processedBytes >= static_cast<int>(kFrameByteSize)) {
        const char *frameData = pendingPcm_.constData() + processedBytes;
        const auto *frameBytes = reinterpret_cast<const uchar *>(frameData);

        for (std::size_t i = 0; i < kFrameSampleCount; ++i) {
            frameSamples[i] = qFromLittleEndian<int16_t>(frameBytes + i * sizeof(int16_t));
        }

        const std::optional<bool> isSpeech = classifier_->processFrame(frameSamples.data(), frameSamples.size(), errorMessage);
        if (!isSpeech.has_value()) {
            clear();
            return false;
        }

        consumeFrameDecision(*isSpeech);
        processedBytes += static_cast<int>(kFrameByteSize);

        if (autoStop_) {
            break;
        }
    }

    if (processedBytes > 0) {
        pendingPcm_.remove(0, processedBytes);
    }

    return true;
}

bool VadEndpointDetector::shouldAutoStop() const
{
    return autoStop_;
}

VadEndpointConfig VadEndpointDetector::sanitizeConfig(const VadEndpointConfig &config)
{
    VadEndpointConfig normalized = config;
    if (normalized.aggressiveness < 0 || normalized.aggressiveness > 3) {
        normalized.aggressiveness = 2;
    }
    if (normalized.endSilenceMs <= 0) {
        normalized.endSilenceMs = 900;
    }
    if (normalized.minSpeechMs <= 0) {
        normalized.minSpeechMs = 250;
    }
    return normalized;
}

void VadEndpointDetector::consumeFrameDecision(bool isSpeech)
{
    if (isSpeech) {
        contiguousSpeechMs_ += kFrameMs;
        trailingSilenceMs_ = 0;
        if (contiguousSpeechMs_ >= config_.minSpeechMs) {
            hasQualifiedSpeech_ = true;
        }
        return;
    }

    contiguousSpeechMs_ = 0;
    if (!hasQualifiedSpeech_) {
        return;
    }

    trailingSilenceMs_ += kFrameMs;
    if (trailingSilenceMs_ >= config_.endSilenceMs) {
        autoStop_ = true;
    }
}
