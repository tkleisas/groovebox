#pragma once

// Audio input capture source for the SAMPLE page.
//
// Two implementations behind one interface so tests can feed
// generated audio headlessly:
//   * EngineCaptureSource — yawn's PrivateCaptureRequest side channel
//     (AudioEngine::startPrivateCapture): mono input ch 0 from the
//     already-open PortAudio stream. On the Pi this same code reads
//     the WM8731 codec's capture device.
//   * SineCaptureSource — deterministic generated take for tests.

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include "audio/AudioEngine.h"

namespace gb {

class CaptureSource {
public:
    virtual ~CaptureSource() = default;
    virtual bool start(int frames) = 0;  // false = no input available
    virtual void stop() = 0;
    virtual bool done() const = 0;
    virtual int framesWritten() const = 0;
    virtual int totalFrames() const = 0;
    virtual const float* data() const = 0;
    virtual float recentPeak() = 0;      // peak since last call (decays)
};

class EngineCaptureSource : public CaptureSource {
public:
    explicit EngineCaptureSource(yawn::audio::AudioEngine& e)
        : m_engine(e) {}

    bool start(int frames) override {
        m_req = std::make_unique<
            yawn::audio::AudioEngine::PrivateCaptureRequest>();
        m_req->inputChannel = 0;
        m_req->channels = 1;
        m_req->frames = frames;
        m_req->buffer.resize(size_t(frames), 0.0f);
        m_scanPos = 0;
        m_peak = 0.0f;
        return m_engine.startPrivateCapture(m_req.get());
    }
    void stop() override { m_engine.abortPrivateCapture(); }
    bool done() const override { return !m_req || m_req->done.load(); }
    int framesWritten() const override {
        return m_req ? int(m_req->framesWritten.load()) : 0;
    }
    int totalFrames() const override { return m_req ? int(m_req->frames) : 0; }
    const float* data() const override {
        return m_req ? m_req->buffer.data() : nullptr;
    }
    float recentPeak() override {
        if (!m_req) return 0.0f;
        const int fw = framesWritten();
        for (int i = m_scanPos; i < fw; ++i) {
            const float a = std::fabs(m_req->buffer[size_t(i)]);
            if (a > m_peak) m_peak = a;
        }
        m_scanPos = fw;
        const float p = m_peak;
        m_peak *= 0.90f; // meter decay
        return p;
    }

private:
    yawn::audio::AudioEngine& m_engine;
    std::unique_ptr<yawn::audio::AudioEngine::PrivateCaptureRequest> m_req;
    int m_scanPos = 0;
    float m_peak = 0.0f;
};

// Deterministic test source: a 220 Hz sine with a noise floor and an
// attack/decay envelope, produced progressively in real time.
class SineCaptureSource : public CaptureSource {
public:
    bool start(int frames) override {
        m_buf.resize(size_t(frames));
        uint32_t rng = 12345;
        for (int i = 0; i < frames; ++i) {
            const double t = i / 48000.0;
            rng = rng * 1664525u + 1013904223u;
            const double noise = int(rng >> 16) / 32768.0 - 1.0;
            const double env = std::min(1.0, t * 50.0) *
                               (t > 0.8 ? std::exp(-(t - 0.8) * 10.0) : 1.0);
            m_buf[size_t(i)] = float(
                (0.7 * std::sin(2.0 * 3.14159265358979 * 220.0 * t) +
                 0.05 * noise) * env);
        }
        m_frames = frames;
        m_t0 = std::chrono::steady_clock::now();
        m_scanPos = 0;
        m_peak = 0.0f;
        return true;
    }
    void stop() override {
        if (!m_stopped) { m_stopFrames = framesWritten(); m_stopped = true; }
    }
    bool done() const override {
        return m_stopped || framesWritten() >= m_frames;
    }
    int framesWritten() const override {
        if (m_stopped) return m_stopFrames;
        const double el = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_t0).count();
        return std::min(m_frames, int(el * 48000.0));
    }
    int totalFrames() const override { return m_frames; }
    const float* data() const override { return m_buf.data(); }
    float recentPeak() override {
        const int fw = framesWritten();
        for (int i = m_scanPos; i < fw; ++i) {
            const float a = std::fabs(m_buf[size_t(i)]);
            if (a > m_peak) m_peak = a;
        }
        m_scanPos = fw;
        const float p = m_peak;
        m_peak *= 0.90f;
        return p;
    }

private:
    std::vector<float> m_buf;
    int m_frames = 0;
    int m_scanPos = 0;
    int m_stopFrames = 0;
    float m_peak = 0.0f;
    bool m_stopped = false;
    std::chrono::steady_clock::time_point m_t0;
};

} // namespace gb
