#include "wheel_output.h"

// We provide our own WinMain, so SDL must not hijack `main`.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <cmath>

// Map our normalized signals (-1..1 / 0..1) onto SDL's Sint16 force range.
static constexpr int SDL_FORCE_MAX = 32767;

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Final guard before the hardware: a non-finite value (NaN/Inf) would be
// undefined behaviour when cast to int and could send a garbage level to a
// direct-drive wheel. clampf does not catch NaN, so scrub it to 0 here.
static float finiteOrZero(float v) {
    return std::isfinite(v) ? v : 0.f;
}

// ── Lifetime ──────────────────────────────────────────────────────────────────
WheelOutput::WheelOutput() {}

WheelOutput::~WheelOutput() {
    closeDevice();
    if (m_inited) {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
        SDL_Quit();
        m_inited = false;
    }
}

bool WheelOutput::init() {
    SDL_SetMainReady();   // required when SDL_MAIN_HANDLED is defined
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0) {
        m_error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }
    m_inited = true;
    enumerate();
    return true;
}

// ── Enumeration ────────────────────────────────────────────────────────────────
void WheelOutput::enumerate() {
    m_devices.clear();
    if (!m_inited) return;

    SDL_JoystickUpdate();

    // Enumerate by haptic index. Many DD bases appear here even when
    // SDL_JoystickIsHaptic() reports 0 on the steering device.
    int n = SDL_NumHaptics();
    for (int i = 0; i < n; i++) {
        const char* nm = SDL_HapticName(i);
        m_devices.push_back({ i, nm ? std::string(nm) : std::string("Unknown device") });
    }
}

// ── Open / close ───────────────────────────────────────────────────────────────
bool WheelOutput::openDevice(int index) {
    std::lock_guard<std::mutex> lk(m_lock);
    closeDeviceLocked();

    if (index < 0 || index >= (int)m_devices.size()) {
        m_error = "Invalid device index";
        return false;
    }

    SDL_Haptic* h = SDL_HapticOpen(m_devices[index].hapticIndex);
    if (!h) {
        m_error = std::string("Could not open force feedback: ") + SDL_GetError()
                + ". Close your wheel's tuning software and any game using the "
                  "wheel, set in-game FFB to 0%, then try again.";
        return false;
    }

    unsigned int caps = SDL_HapticQuery(h);
    if (!(caps & SDL_HAPTIC_CONSTANT)) {
        m_error = "Device does not support constant-force FFB (required).";
        SDL_HapticClose(h);
        return false;
    }

    // Disable the base's autocentre spring and request full motor gain.
    SDL_HapticSetAutocenter(h, 0);
    SDL_HapticSetGain(h, 100);

    // ── Constant force (main torque) — mandatory ──────────────────────────────
    SDL_HapticEffect cf{};
    cf.type                 = SDL_HAPTIC_CONSTANT;
    cf.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    cf.constant.direction.dir[0] = 1;
    cf.constant.length      = SDL_HAPTIC_INFINITY;
    cf.constant.level       = 0;
    m_effConstant = SDL_HapticNewEffect(h, &cf);
    if (m_effConstant < 0) {
        m_error = std::string("Failed to upload constant-force effect: ") + SDL_GetError();
        SDL_HapticClose(h);
        return false;
    }
    if (SDL_HapticRunEffect(h, m_effConstant, 1) < 0) {
        m_error = std::string("Failed to start constant-force effect: ") + SDL_GetError();
        SDL_HapticDestroyEffect(h, m_effConstant);
        m_effConstant = -1;
        SDL_HapticClose(h);
        return false;
    }

    // ── Damper (friction) — optional ──────────────────────────────────────────
    if (caps & SDL_HAPTIC_DAMPER) {
        SDL_HapticEffect dp{};
        dp.type           = SDL_HAPTIC_DAMPER;
        dp.condition.length = SDL_HAPTIC_INFINITY;
        m_effDamper = SDL_HapticNewEffect(h, &dp);
        if (m_effDamper >= 0) SDL_HapticRunEffect(h, m_effDamper, 1);
    }

    // ── Sine (kerb / lockup rumble) — optional ────────────────────────────────
    if (caps & SDL_HAPTIC_SINE) {
        SDL_HapticEffect si{};
        si.type                   = SDL_HAPTIC_SINE;
        si.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        si.periodic.direction.dir[0] = 1;
        si.periodic.period        = 50;   // ms → 20 Hz
        si.periodic.magnitude     = 0;
        si.periodic.length        = SDL_HAPTIC_INFINITY;
        m_effSine = SDL_HapticNewEffect(h, &si);
        if (m_effSine >= 0) SDL_HapticRunEffect(h, m_effSine, 1);
    }

    m_haptic    = h;
    m_activeIdx = index;
    m_error.clear();
    m_sendOk.store(0);
    m_sendErrors.store(0);
    m_lastDamperLvl  = LVL_UNSET;   // force the first damper/sine update through
    m_lastSineLvl    = LVL_UNSET;
    m_lastSinePeriod = LVL_UNSET;
    return true;
}

std::string WheelOutput::lastError() const {
    std::lock_guard<std::mutex> lk(m_lock);
    return m_error;
}

void WheelOutput::releaseEffects() {
    if (!m_haptic) return;
    SDL_Haptic* h = static_cast<SDL_Haptic*>(m_haptic);
    auto drop = [&](int& id) {
        if (id >= 0) {
            SDL_HapticStopEffect(h, id);
            SDL_HapticDestroyEffect(h, id);
            id = -1;
        }
    };
    drop(m_effConstant);
    drop(m_effDamper);
    drop(m_effSine);
}

// Public close: takes the lock so the device can never be torn down while the
// FFB thread is mid-send(). openDevice (already holding the lock) uses the
// unlocked body directly.
void WheelOutput::closeDevice() {
    std::lock_guard<std::mutex> lk(m_lock);
    closeDeviceLocked();
}

void WheelOutput::closeDeviceLocked() {
    releaseEffects();
    if (m_haptic) {
        SDL_HapticClose(static_cast<SDL_Haptic*>(m_haptic));
        m_haptic = nullptr;
    }
    m_activeIdx = -1;
}

// ── Per-frame send (FFB thread) ────────────────────────────────────────────────
void WheelOutput::send(const FFBSignals& sig) {
    std::lock_guard<std::mutex> lk(m_lock);
    if (!m_haptic) return;
    SDL_Haptic* h = static_cast<SDL_Haptic*>(m_haptic);

    // Constant force — main torque
    if (m_effConstant >= 0) {
        SDL_HapticEffect e{};
        e.type                  = SDL_HAPTIC_CONSTANT;
        e.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        e.constant.direction.dir[0] = 1;
        e.constant.length       = SDL_HAPTIC_INFINITY;
        int lvl = (int)(clampf(finiteOrZero(sig.torque), -1.f, 1.f) * SDL_FORCE_MAX);
        e.constant.level        = (Sint16)clampi(lvl, -SDL_FORCE_MAX, SDL_FORCE_MAX);
        if (SDL_HapticUpdateEffect(h, m_effConstant, &e) < 0) {
            if (m_sendErrors.fetch_add(1) == 0)   // capture the first error only
                m_error = std::string("SDL_HapticUpdateEffect: ") + SDL_GetError();
        } else {
            m_sendOk.fetch_add(1);
        }
    }

    // Damper — friction. Skip the driver round-trip when the level is unchanged.
    if (m_effDamper >= 0) {
        int coeff = (int)(clampf(finiteOrZero(sig.friction), 0.f, 1.f) * SDL_FORCE_MAX);
        if (coeff != m_lastDamperLvl) {
            SDL_HapticEffect e{};
            e.type            = SDL_HAPTIC_DAMPER;
            e.condition.length = SDL_HAPTIC_INFINITY;
            for (int a = 0; a < 3; a++) {
                e.condition.right_coeff[a] = (Sint16)coeff;
                e.condition.left_coeff[a]  = (Sint16)coeff;
                e.condition.right_sat[a]   = SDL_FORCE_MAX;
                e.condition.left_sat[a]    = SDL_FORCE_MAX;
            }
            SDL_HapticUpdateEffect(h, m_effDamper, &e);
            m_lastDamperLvl = coeff;
        }
    }

    // Sine — rumble. The engine varies the frequency with the effect source
    // (lockup = fast judder, wheelspin = slow tramp). The pitch is applied only
    // when a rumble episode STARTS (silent → audible): re-sending a changed
    // period to a PLAYING effect makes some DirectInput drivers stop/restart it
    // on every update, which can mute the effect entirely. While audible, only
    // the magnitude is updated — the traffic pattern proven to work.
    if (m_effSine >= 0) {
        int mag = (int)(clampf(finiteOrZero(sig.rumble), 0.f, 1.f) * SDL_FORCE_MAX);
        float hzf = clampf(finiteOrZero(sig.rumbleHz), 5.f, 80.f);
        int period = clampi((int)(1000.f / hzf + 0.5f), 12, 200);   // ms
        if (m_lastSinePeriod == LVL_UNSET || (mag > 0 && m_lastSineLvl <= 0))
            m_lastSinePeriod = period;   // episode start: latch the new pitch
        if (mag != m_lastSineLvl) {
            SDL_HapticEffect e{};
            e.type                   = SDL_HAPTIC_SINE;
            e.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
            e.periodic.direction.dir[0] = 1;
            e.periodic.period        = (Uint16)m_lastSinePeriod;
            e.periodic.magnitude     = (Sint16)mag;
            e.periodic.length        = SDL_HAPTIC_INFINITY;
            if (SDL_HapticUpdateEffect(h, m_effSine, &e) < 0) {
                if (m_sendErrors.fetch_add(1) == 0)   // capture the first error only
                    m_error = std::string("SDL_HapticUpdateEffect (rumble): ") + SDL_GetError();
            }
            m_lastSineLvl = mag;
        }
    }
}
