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

    // Diagnostic: steady rumble at the Lockup Pitch, bypassing telemetry —
    // proves the rumble channel works on this base (and auditions the pitch
    // sliders) independent of slip detection ever firing.
    if (c.testRumble > 0.001f) {
        float r = clampf(c.testRumble, 0.f, 1.f);
        m_smoothTorque = 0.f; m_smoothFriction = 0.f; m_smoothRumble = r;
        return { 0.f, 0.f, r, c.lockupHz };
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

        // ── AI start hold (Time Trial / One-Shot & Hot-Lap Qualifying) ───────
        // In these modes the AI drives the car at the start of a lap — entering
        // the mode, a menu restart, or the flying run-up to the line in one-shot
        // qualifying — before handing control to the player. All of them go
        // through a load, which shows up as a freeze→advance RE-ARM and/or a
        // lap-distance RESET. We arm the hold on a re-arm or on entering the
        // mode, then later cancel it ONLY if it was actually a mid-lap UNPAUSE —
        // i.e. no lap reset happened AND we're deep into the lap. A restart
        // resets the lap (kept); a one-shot flying start sits before the line so
        // lapDistance is negative (kept); a flying-lap line-crossing has a reset
        // but NO re-arm so it never arms — a hot lap in progress keeps its
        // force. Scoped via sessionHasAiLapStart so race-start launches
        // (player-controlled) are unaffected.
        const uint8_t sess      = m_stats.sessionType.load(std::memory_order_relaxed);
        const bool aiStartMode  = sessionHasAiLapStart(sess);
        const bool enteredMode  = aiStartMode && !sessionHasAiLapStart(m_prevSessionType);
        m_prevSessionType       = sess;

        const float lapDist    = m_stats.lapDistance.load(std::memory_order_relaxed);
        const bool dropped     = (lapDist < m_prevLapDist - 100.f);
        m_prevLapDist          = lapDist;
        const bool rearmed     = (streak != m_prevStreak);
        m_prevStreak           = streak;

        if (dropped) m_lastDropMs = now;   // remember the last lap reset (restart marker)

        bool ttHold = false;
        if (aiStartMode && c.ttStartHoldSec > 0.01f) {
            if (rearmed || enteredMode) {                  // a load just finished
                m_ttHoldUntilMs = now + (int64_t)(c.ttStartHoldSec * 1000.f);
                m_ttCheckMs     = now + 2500;              // classify late: the lap reset can lag
                m_ttPending     = true;
            }
            if (m_ttPending && now >= m_ttCheckMs) {
                m_ttPending = false;
                // Cancel ONLY for a clear mid-lap unpause: no lap reset happened
                // anywhere near this event AND we're still deep into the lap. A
                // restart always resets the lap (even if it lags), so it's kept;
                // when in doubt we hold (the safe direction). Reset timestamp is
                // used (not a flag) so a later re-arm can't wipe the evidence.
                const bool recentReset = (now - m_lastDropMs) < 2700;
                if (!recentReset && lapDist > 250.f)
                    m_ttHoldUntilMs = 0;
            }
            ttHold = (now < m_ttHoldUntilMs);
        }
        m_stats.ttHolding.store(ttHold, std::memory_order_relaxed);

        if (frozen || rearming || paused || aiDriving || ttHold) {
            m_smoothTorque = 0.f; m_smoothFriction = 0.f; m_smoothRumble = 0.f;
            m_softGain = 0.f;     // restart the soft-start ramp on next resume
            m_clipEma  = 0.f;
            m_prevSteer = s.steer; m_steerVelLP = 0.f;   // no braking-weight spike on resume
            m_latForceLP = 0.f;   // peak tracker ramps fresh on resume
            m_stats.clipLevel.store(0.f, std::memory_order_relaxed);
            return { 0.f, 0.f, 0.f };
        }
    }

    // ── Auto Max Force: track the peak sustained front lateral force ─────────
    // A ~100ms low-pass rejects single-frame spikes (kerb strikes, half-spins)
    // that a real corner wouldn't sustain, so the recorded peak reflects genuine
    // cornering load. The GUI offers one-click "set Full-Scale Force to this".
    // Only measured here, while armed — paused/AI/TT frames never pollute it.
    {
        const float aF = clampf(dt / (0.10f + dt), 0.f, 1.f);
        m_latForceLP += aF * (std::fabs(s.frontLatForce) - m_latForceLP);
        if (m_latForceLP > m_stats.peakLatForceN.load(std::memory_order_relaxed))
            m_stats.peakLatForceN.store(m_latForceLP, std::memory_order_relaxed);
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
    m_state.understeer    = understeer;
    m_state.oversteer     = oversteer;
    m_state.frontSlipNorm = frontNorm;
    m_state.rearSlipNorm  = rearNorm;

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
    // Extra damping when sliding — stops DD wheel oscillation. (Braking weight
    // used to ride this damper channel too, but many bases — direct-drive
    // especially — ignore the hardware DAMPER effect, so it was never felt.
    // It's synthesized on the constant-force channel instead — see section 7b.)
    float friction = clampf(0.08f + (frontNorm + rearNorm) * 0.10f, 0.f, 0.4f);

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

    const float lockupAmt = lockup * c.lockupStrength;
    const float spinAmt   = spin   * c.wheelspinStrength;
    rumble = clampf(rumble + lockupAmt + spinAmt, 0.f, 1.f);

    // Rumble frequency: a locked front tyre reads as a fast judder, rear
    // wheelspin as a slower axle tramp. The base pitch of each is a user
    // setting (wheel bases render periodic effects differently); severity adds
    // up to +30% on top so a deeper lockup/spin buzzes faster. When both fire
    // the frequency blends by their contributions. Held at its last value
    // while silent so the smoothed decay tail keeps its pitch.
    const float SEVERITY_RAMP = 0.30f;
    float targetHz = m_rumbleHz;
    if (lockupAmt + spinAmt > 1e-4f) {
        const float lockupHz = c.lockupHz    * (1.f + SEVERITY_RAMP * lockup);
        const float spinHz   = c.wheelspinHz * (1.f + SEVERITY_RAMP * spin);
        targetHz = (lockupAmt * lockupHz + spinAmt * spinHz) / (lockupAmt + spinAmt);
    }

    // ── 7b. Braking weight (software damper) ─────────────────────────────────
    // The hardware DAMPER channel is ignored by many bases (direct-drive
    // especially), so braking weight is synthesized here on the constant-force
    // channel that every wheel renders: a damper that resists steering motion
    // under braking (heavier to turn), gated by brake pedal × front longitudinal
    // load. Folded into the main torque so it rides Overall Strength, the speed
    // fade and clip detection. Hard-capped for DD safety; direction follows
    // Invert Force, with a dedicated Invert Braking Weight override for bases
    // where the sign fights the wheel.
    float brakeLoad = clampf(std::fabs(s.frontLonForce) / 18000.f, 0.f, 1.f)
                    * clampf(s.brake * 1.5f, 0.f, 1.f);
    float brakeWeight = c.brakingStrength * brakeLoad;
    if (brakeWeight > 1e-4f) {
        // Steering velocity from telemetry steer. Telemetry is ~60Hz but we run
        // faster, so the raw difference is impulsive — low-pass it (~30ms).
        float rawSteerVel = (s.steer - m_prevSteer) / (dt > 1e-4f ? dt : 1e-4f);
        const float aSteer = clampf(dt / (0.03f + dt), 0.f, 1.f);
        m_steerVelLP += aSteer * (rawSteerVel - m_steerVelLP);

        float damper = -clampf(m_steerVelLP, -6.f, 6.f) * 0.06f;   // oppose motion
        float bw = clampf(damper * brakeWeight, -0.30f, 0.30f);

        const float dir = (c.invertForce   ? -1.f : 1.f)
                        * (c.brakingInvert ? -1.f : 1.f);
        torque += dir * bw;
    }
    m_prevSteer = s.steer;

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
    // Glide the frequency too so alternating lockup/spin dominance can't make
    // the pitch jump around frame to frame.
    m_rumbleHz       += aR * (targetHz - m_rumbleHz);

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
        clampf(m_smoothRumble   * g,  0.f, cap),
        m_rumbleHz
    };
}
