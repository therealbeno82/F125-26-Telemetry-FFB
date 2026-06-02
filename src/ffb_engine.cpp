#include "ffb_engine.h"
#include <windows.h>
#include <chrono>
#include <cmath>

FFBEngine::FFBEngine(TelemetryState& state, FFBSettings& settings,
                     AppStats& stats, FFBCallback callback)
    : m_state(state), m_settings(settings),
      m_stats(stats), m_callback(std::move(callback)) {}

FFBEngine::~FFBEngine() { stop(); }

bool FFBEngine::start() {
    m_running = true;
    m_thread  = std::thread(&FFBEngine::loop, this);
    return true;
}

void FFBEngine::stop() {
    m_running = false;
    // Wake the engine if it's blocked waiting for the next telemetry frame, so
    // it sees m_running == false and exits promptly instead of after the keepalive.
    {
        std::lock_guard<std::mutex> lk(m_stats.wakeMutex);
    }
    m_stats.wakeCv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void FFBEngine::loop() {
    // Use MMCSS for real-time scheduling — this gives us 1ms timer resolution
    // and prevents Windows scheduler from bumping us out for 15ms at a time
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    timeBeginPeriod(1);

    using Clock = std::chrono::high_resolution_clock;
    using ns    = std::chrono::nanoseconds;

    auto prev = Clock::now();
    uint64_t seenSeq = m_stats.motionSeq.load(std::memory_order_relaxed);

    while (m_running) {
        // The configured rate is now a CEILING, not a fixed poll rate: it's the
        // minimum gap between sends (so the wheel is never updated faster than it
        // can handle). Clamped to guard a stale/out-of-range value.
        int hz = m_settings.ffbUpdateHz;
        if (hz < FFB_MIN_HZ) hz = 90;
        if (hz > FFB_MAX_HZ) hz = FFB_MAX_HZ;
        const auto interval = ns(1'000'000'000LL / hz);

        const auto t0 = Clock::now();

        // Real elapsed time since the last cycle drives frame-rate-independent
        // smoothing. Guard the first iteration and any post-stall resume.
        float dt = std::chrono::duration<float>(t0 - prev).count();
        prev = t0;
        if (dt <= 0.f || dt > 0.25f) dt = 1.f / hz;

        FFBSignals sig = compute(dt);
        m_callback(sig);
        m_stats.ffbUpdates.fetch_add(1, std::memory_order_relaxed);

        // ── Event-driven wait ────────────────────────────────────────────────
        // Block until a fresh telemetry frame arrives (the UDP thread bumps
        // motionSeq + notifies), so we compute on new data with near-zero added
        // latency instead of polling on an independent clock. Fall back to a
        // short keepalive so smoothing / soft-start / the safety release keep
        // running if telemetry stalls (game closed, alt-tabbed, network drop).
        const auto stallDeadline = t0 + ns(FFB_KEEPALIVE_MS * 1'000'000LL);
        {
            std::unique_lock<std::mutex> lk(m_stats.wakeMutex);
            m_stats.wakeCv.wait_until(lk, stallDeadline, [&] {
                return !m_running.load(std::memory_order_relaxed)
                     || m_stats.motionSeq.load(std::memory_order_relaxed) != seenSeq;
            });
            seenSeq = m_stats.motionSeq.load(std::memory_order_relaxed);
        }
        if (!m_running) break;

        // Honour the ceiling: never send closer together than `interval`. If the
        // frame arrived sooner, hold here (sleep then busy-spin the last 500µs).
        const auto earliestNext = t0 + interval;
        const auto sleepUntil   = earliestNext - ns(500'000);
        if (Clock::now() < sleepUntil) {
            std::this_thread::sleep_until(sleepUntil);
        }
        while (Clock::now() < earliestNext) { /* spin */ }
    }

    timeEndPeriod(1);
}

FFBSignals FFBEngine::compute(float dt) {
    // Take a snapshot of telemetry (written by UDP thread)
    // We read without a mutex here — these are all floats written atomically
    // on x86 and a frame-old read is acceptable for FFB
    const TelemetryState s = m_state;
    const FFBSettings&   c = m_settings;

    // Diagnostic: a steady test force bypasses all physics so the wheel can be
    // verified independent of telemetry (no game / no movement required).
    if (std::fabs(c.testForce) > 0.001f) {
        float t = clampf(c.testForce, -1.f, 1.f);
        m_smoothTorque = t; m_smoothFriction = 0.f; m_smoothRumble = 0.f;
        return { t, 0.f, 0.f };
    }

    // ── SAFETY: release the wheel unless the car is actively being driven ────
    // Two independent pause signals, covering both game modes:
    //  1. Frame freeze — single-player / time-trial pause and menus FREEZE the
    //     physics frame counter (even though packets keep arriving). We require
    //     the frame to be advancing, plus a re-arm delay so a single stray menu
    //     frame can't flick force back on. Also covers exit / minimise.
    //  2. m_gamePaused — an ONLINE session keeps simulating while paused (AI
    //     drives the car), so the frame counter does NOT freeze. The Session
    //     packet's pause flag is the only thing that reveals the player is in
    //     the menu. Without this the wheel thrashes against the AI's driving.
    {
        using namespace std::chrono;
        const int64_t now    = duration_cast<milliseconds>(
                                   steady_clock::now().time_since_epoch()).count();
        const int64_t adv    = m_stats.lastFrameAdvanceMs.load(std::memory_order_relaxed);
        const int64_t streak = m_stats.frameStreakStartMs.load(std::memory_order_relaxed);

        const bool frozen    = (adv == 0) || (now - adv > FFB_PAUSE_FREEZE_MS);
        const bool rearming  = (now - streak < FFB_REARM_MS);
        const bool paused    = m_stats.gamePaused.load(std::memory_order_relaxed);
        // AI has the car (race/quali/session finished, or pit-lane auto-drive) —
        // the player isn't driving, so release the wheel.
        const bool aiDriving = m_stats.aiInControl.load(std::memory_order_relaxed);
        if (frozen || rearming || paused || aiDriving) {
            m_smoothTorque = 0.f; m_smoothFriction = 0.f; m_smoothRumble = 0.f;
            m_softGain = 0.f;     // restart the soft-start ramp on next resume
            m_clipEma  = 0.f;
            m_stats.clipLevel.store(0.f, std::memory_order_relaxed);
            return { 0.f, 0.f, 0.f };
        }
    }

    const float speed = s.speedKmh;

    // Speed fade: smooth ramp from minSpeed to minSpeed+20 km/h
    const float speedFactor = clampf((speed - c.minSpeedKmh) / 20.f, 0.f, 1.f);

    // ── 1. Base torque: front tyre lateral force (true self-aligning torque) ──
    // wheelLatForce is the actual force at the contact patch (N). Unlike lateral
    // G, it naturally saturates: as the front slides the force plateaus then
    // drops, so the wheel goes light during understeer for free — no modelling.
    // Guard the divisor: a corrupt/hand-edited profile could carry 0 here, and
    // 0/0 = NaN sails through clampf (NaN compares false both ways) onto the wheel.
    const float maxForceN = c.maxForceN > 1.f ? c.maxForceN : 1.f;
    float baseTorque = clampf(s.frontLatForce / maxForceN, -1.f, 1.f);
    if (c.invertForce) baseTorque = -baseTorque;

    // ── 1b. Load-sensitive steering weight (optional) ────────────────────────
    // wheelVertForce is the downward load on the front tyres (N) — mechanical
    // weight + aero downforce + braking weight transfer. The real self-aligning
    // torque scales with this load, so optionally lighten the wheel when the
    // front is unloaded (low speed, cresting a rise) and keep it full under high
    // load (aero at speed, trail-braking). Blended in by loadSensitivity; 0 =
    // original feel. Only ever attenuates, so it never adds new clipping.
    if (c.loadSensitivity > 1e-4f && c.loadRefN > 1.f) {
        const float LOAD_FLOOR = 0.4f;   // lightest the wheel gets at zero load
        float loadNorm = clampf(s.frontVertForce / c.loadRefN, 0.f, 1.f);
        float loadMul  = LOAD_FLOOR + (1.f - LOAD_FLOOR) * loadNorm;
        baseTorque *= 1.f + c.loadSensitivity * (loadMul - 1.f);
    }

    // ── 2. Slip angles → grip loss signals (for GUI + extra cues) ───────────
    // 8° of slip angle = fully saturated tyre
    const float SLIP_SCALE = 8.f * (3.14159f / 180.f);  // rad

    float frontNorm = clampf(s.frontSlipAngle / SLIP_SCALE, 0.f, 1.f);
    float rearNorm  = clampf(s.rearSlipAngle  / SLIP_SCALE, 0.f, 1.f);

    // Understeer: front slips more than rear → wheel goes light
    float understeer = clampf(frontNorm - rearNorm * 0.5f, 0.f, 1.f);

    // Oversteer: rear slips significantly more than front → counter-kick
    float oversteer  = clampf(rearNorm  - frontNorm * 0.3f, 0.f, 1.f);

    // Write derived signals back for the GUI to display
    // (the engine owns these fields so no lock needed)
    const_cast<TelemetryState&>(m_state).understeer   = understeer;
    const_cast<TelemetryState&>(m_state).oversteer    = oversteer;
    const_cast<TelemetryState&>(m_state).frontSlipNorm = frontNorm;
    const_cast<TelemetryState&>(m_state).rearSlipNorm  = rearNorm;

    float torque = baseTorque;

    // ── 3. Extra understeer lightening on top of the natural force drop ──────
    // The force signal already captures most of this; this adds optional
    // exaggeration. Strengths may exceed 1.0 for a stronger cue, so clamp the
    // result to [0,1]: a heavy setting can take the wheel fully light but must
    // never push past zero and invert the torque direction.
    float usReduction = understeer * c.understeerStrength * c.gripLossStrength * 0.4f;
    torque *= clampf(1.f - usReduction, 0.f, 1.f);

    // ── 4. Oversteer counter-steer cue from vehicle sideslip angle ──────────
    // Sideslip is signed and proportional to how far the rear has stepped out,
    // so it pushes the wheel the direction you'd counter-steer to catch it.
    // Gated by rear slip so it only fires during genuine oversteer.
    const float SIDESLIP_SCALE = 12.f * (3.14159f / 180.f);  // 12° = full cue
    float slipCue = clampf(s.sideslip / SIDESLIP_SCALE, -1.f, 1.f);
    if (c.invertForce) slipCue = -slipCue;   // keep cue aligned with base torque
    torque += slipCue * oversteer * c.oversteerStrength * 0.5f;

    // ── 5. Friction / damping ────────────────────────────────────────────────
    // Extra damping when sliding — stops DD wheel oscillation
    float friction = clampf(0.08f + (frontNorm + rearNorm) * 0.10f, 0.f, 0.4f);

    // Braking weight: front tyres load up under braking, so firm the wheel.
    // Gated by the brake pedal, which also disambiguates the lon-force sign.
    float brakeLoad = clampf(std::fabs(s.frontLonForce) / 18000.f, 0.f, 1.f)
                    * clampf(s.brake * 1.5f, 0.f, 1.f);
    friction += brakeLoad * c.brakingStrength * 0.3f;

    // ── 6. Rumble channel ─────────────────────────────────────────────────
    // Kerb rumble (suspension velocity) was removed: it buzzed constantly since
    // suspension velocity is never quite zero, and kerbs already come through
    // the torque/load changes. Only the gated effects below feed the rumble.
    float rumble = 0.f;

    // ── 7. Lockup judder + wheelspin rumble from slip ratio ─────────────────
    // Lockup: front wheels turning slower than the road (slip ratio negative).
    // Gated by brake so it only fires when threshold-braking, not coasting.
    const float LOCKUP_DEADZONE = 0.06f, LOCKUP_RANGE = 0.30f;
    float lockup = clampf((-s.frontSlipRatio - LOCKUP_DEADZONE) / LOCKUP_RANGE, 0.f, 1.f)
                 * clampf(s.brake * 2.f, 0.f, 1.f);

    // Wheelspin: rear wheels spinning faster than the road (slip ratio positive).
    const float SPIN_DEADZONE = 0.10f, SPIN_RANGE = 0.50f;
    float spin = clampf((s.rearSlipRatio - SPIN_DEADZONE) / SPIN_RANGE, 0.f, 1.f)
               * clampf(s.throttle * 2.f, 0.f, 1.f);

    rumble = clampf(rumble + lockup * c.lockupStrength
                           + spin   * c.wheelspinStrength, 0.f, 1.f);

    // ── 8. Overall scale + speed fade ────────────────────────────────────────
    const float scale = c.overallStrength * speedFactor;
    const float torquePre = torque * scale;          // before clamp, for clip detect
    torque   = clampf(torquePre,        -1.f, 1.f);
    friction = clampf(friction * scale,  0.f, 1.f);
    rumble   = clampf(rumble   * scale,  0.f, 1.f);

    // Clipping: the torque demand exceeded the ±1 output range, so detail is
    // being lost — the user should lower Overall Strength or raise Max Force.
    // Track a rolling fraction (EMA) so the GUI can show how often we clip.
    const bool clipping = std::fabs(torquePre) > 0.999f;
    m_clipEma += 0.04f * ((clipping ? 1.f : 0.f) - m_clipEma);
    m_stats.clipLevel.store(m_clipEma, std::memory_order_relaxed);

    // ── 9. Time-based smoothing / interpolation ──────────────────────────────
    // Frame-rate independent: behaves the same at any output rate, and glides
    // the output smoothly between the 60Hz telemetry frames. The higher the
    // Update Rate, the finer the interpolation toward each new telemetry target
    // — this is the "upscaling" from 60Hz to the wheel's output rate.
    const float tau = c.smoothing * 0.08f;   // smoothing time constant, 0..40ms
    const float a   = tau > 1e-5f ? clampf(dt / (tau + dt),         0.f, 1.f) : 1.f;
    const float aR  = tau > 1e-5f ? clampf(dt / (tau * 0.5f + dt),  0.f, 1.f) : 1.f;
    m_smoothTorque   += a  * (torque   - m_smoothTorque);
    m_smoothFriction += a  * (friction - m_smoothFriction);
    m_smoothRumble   += aR * (rumble   - m_smoothRumble);

    // ── 10. Min-force: deadzone removal for belt/gear wheels ─────────────────
    // Belt/gear bases have static friction that swallows small forces, so the
    // wheel feels dead around centre. Remap any non-zero torque magnitude into
    // [minForce..1] so the smallest cue is still felt; a true zero stays zero.
    float outTorque = m_smoothTorque;
    const float mf = clampf(c.minForce, 0.f, 0.9f);
    if (mf > 1e-4f) {
        float mag = std::fabs(outTorque);
        outTorque = mag > 0.003f ? signf(outTorque) * (mf + (1.f - mf) * mag) : 0.f;
    }

    // ── 11. Soft-start ramp ──────────────────────────────────────────────────
    // Ease force in over softStartSec after the wheel re-arms (connect / unpause)
    // so it never snaps to full — m_softGain was zeroed on the release path.
    const float ramp = c.softStartSec > 1e-3f ? dt / c.softStartSec : 1.f;
    m_softGain = clampf(m_softGain + ramp, 0.f, 1.f);

    // ── 12. Max-output safety cap ────────────────────────────────────────────
    // Hard ceiling on everything the wheel does (e.g. a low cap for a kids
    // profile). Applied last so no other setting can exceed it.
    const float cap = clampf(c.maxOutput, 0.f, 1.f);
    const float g   = m_softGain;
    return {
        clampf(outTorque        * g, -cap, cap),
        clampf(m_smoothFriction * g,  0.f, cap),
        clampf(m_smoothRumble   * g,  0.f, cap)
    };
}
