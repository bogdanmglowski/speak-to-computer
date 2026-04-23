#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

struct VadEndpointConfig {
    int aggressiveness = 2;
    int endSilenceMs = 900;
    int minSpeechMs = 250;
};

class VadFrameClassifier {
public:
    virtual ~VadFrameClassifier() = default;

    virtual bool initialize(int aggressiveness, QString *errorMessage) = 0;
    virtual std::optional<bool> processFrame(
            const int16_t *samples,
            std::size_t sampleCount,
            QString *errorMessage) = 0;
};

class VadEndpointDetector {
public:
    VadEndpointDetector();
    explicit VadEndpointDetector(std::unique_ptr<VadFrameClassifier> classifier);

    bool reset(const VadEndpointConfig &config, QString *errorMessage);
    void clear();
    bool consumePcmChunk(const QByteArray &chunk, QString *errorMessage);
    bool shouldAutoStop() const;

private:
    static VadEndpointConfig sanitizeConfig(const VadEndpointConfig &config);
    void consumeFrameDecision(bool isSpeech);

    std::unique_ptr<VadFrameClassifier> classifier_;
    VadEndpointConfig config_;
    QByteArray pendingPcm_;
    bool armed_ = false;
    bool autoStop_ = false;
    bool hasQualifiedSpeech_ = false;
    int contiguousSpeechMs_ = 0;
    int trailingSilenceMs_ = 0;
};
