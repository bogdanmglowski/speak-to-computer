#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

class QLibrary;

class WebRtcVad {
public:
    WebRtcVad();
    ~WebRtcVad();
    WebRtcVad(const WebRtcVad &) = delete;
    WebRtcVad &operator=(const WebRtcVad &) = delete;
    WebRtcVad(WebRtcVad &&) = delete;
    WebRtcVad &operator=(WebRtcVad &&) = delete;
    static bool isRuntimeAvailable(QString *errorMessage);

    bool initialize(int aggressiveness, QString *errorMessage);
    std::optional<bool> processFrame(const int16_t *samples, std::size_t sampleCount, QString *errorMessage);

private:
    bool ensureRuntimeLoaded(QString *errorMessage);
    void releaseState();

    class Impl;
    Impl *impl_;
};
