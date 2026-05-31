#pragma once
#include "types.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

// Callback invoked from the FFB thread at ~500Hz with the computed signals
using FFBCallback = std::function<void(const FFBSignals&)>;

class FFBEngine {
public:
    FFBEngine(TelemetryState& state, FFBSettings& settings,
              AppStats& stats, FFBCallback callback);
    ~FFBEngine();

    bool start();
    void stop();

private:
    void loop();
    FFBSignals compute(float dt);   // dt = seconds since last cycle (for smoothing)

    TelemetryState& m_state;
    FFBSettings&    m_settings;
    AppStats&       m_stats;
    FFBCallback     m_callback;

    std::thread     m_thread;
    std::atomic<bool> m_running{false};

    // IIR filter state
    float m_smoothTorque   = 0.f;
    float m_smoothFriction = 0.f;
    float m_smoothRumble   = 0.f;

    float m_softGain       = 0.f;   // 0..1 soft-start ramp, reset on release
    float m_clipEma        = 0.f;   // rolling fraction of frames clipping
};
