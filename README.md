# F1 FFB Ã¢â‚¬â€ Telemetry Force Feedback Enhancer
### EA F1 25 / F1 26  Ã‚Â·  Windows  Ã‚Â·  Direct-Drive & belt/gear wheels

[![Latest release](https://img.shields.io/github/v/release/therealbeno82/F125-Telemetry-FFB?include_prereleases&label=latest%20release&color=F59E0B)](https://github.com/therealbeno82/F125-Telemetry-FFB/releases)

A lightweight C++ app that reads F1's UDP telemetry and generates its own force
feedback, sent to your wheel through SDL2's haptic layer. It replaces the game's
built-in FFB with physics-driven forces Ã¢â‚¬â€ base torque from real tyre lateral
force, plus understeer/oversteer cues, lockup and wheelspin rumble, and braking
weight Ã¢â‚¬â€ all tunable live and saved into named profiles.

> **Tested hardware:** All development and testing was done with a **Simucube 2**
> wheel base. Other DirectInput bases should work, but cannot be guaranteed.

Works with **any DirectInput force-feedback wheel** (SimuCube, Fanatec, Moza,
Asetek, Simagic, Logitech, Thrustmaster, Ã¢â‚¬Â¦).

---

## Ã¢Â¬â€¡ Download & run (no setup)

### **[Ã¢Å¾Â¡ Download the latest version (ZIP)](https://github.com/therealbeno82/F125-Telemetry-FFB/releases/download/v2.0/F1FFB-v2.0.zip)**

1. Click the link above to download the ZIP.
2. Right-click it Ã¢â€ â€™ **Extract All**.
3. Run **F1FFB.exe** Ã¢â‚¬â€ that's it. No installer, no extra files (SDL2 is built in).

> **Windows SmartScreen** may warn about an unrecognised app (the build isn't
> code-signed). Click **More info Ã¢â€ â€™ Run anyway**.

Then set up the game's telemetry Ã¢â‚¬â€ see [Turn on telemetry](#3-turn-on-telemetry-in-f1-25--26) below.

*Looking for older versions or release notes? See the [Releases page](https://github.com/therealbeno82/F125-Telemetry-FFB/releases). Want to build it yourself? See [Quick Start](#quick-start).*

---

## Quick Start

### 1. Install prerequisites (one-time)
- **CMake** Ã¢â‚¬â€ https://cmake.org/download/ (tick *Add CMake to PATH* during install)
- **Visual Studio 2022 Build Tools** Ã¢â‚¬â€ https://aka.ms/vs/17/release/vs_BuildTools.exe
  (select the **Desktop development with C++** workload)

### 2. Build
```
build.bat
```
The first build downloads Dear ImGui and SDL2 and compiles everything (a few
minutes). Later builds take seconds. `F1FFB.exe` appears in the project root Ã¢â‚¬â€
it's fully self-contained (SDL2 is linked statically, no DLLs to ship).

### 3. Turn on telemetry in F1 25 / 26
```
Settings Ã¢â€ â€™ Telemetry Settings
  UDP Telemetry:    On
  UDP Broadcast:    Off
  UDP IP Address:   127.0.0.1      (same PC as the game)
  UDP Port:         20777
  UDP Send Rate:    60 Hz
  UDP Format:       2025  (or 2026)
  Your Telemetry:   Public / Unrestricted
```
Then, in **Settings Ã¢â€ â€™ Vibration & Force Feedback**, turn the game's own
**FFB Strength to 0%** Ã¢â‚¬â€ this app drives the wheel instead, and you don't want
the two fighting.

### 4. Run
Launch `F1FFB.exe`, click your wheel in the **Wheel Device** list to connect
(the base's auto-centre spring switching off confirms it's connected), then
drive. The header shows **CONNECTED** once telemetry is flowing.

---

## Using the app

**Connect your wheel** Ã¢â‚¬â€ pick it from the Wheel Device list. If it won't open,
close your wheel's tuning software (True Drive, Tuner, etc.) and any game holding
the wheel, then try again. The list shows the exact error if it fails.

**Test without the game** Ã¢â‚¬â€ tick *Send test force (25%)*. The wheel should pull
and hold to one side. Use this to confirm the wheel responds and to check
direction before driving.

**Profiles** Ã¢â‚¬â€ the *Profile* panel lets you save your tuning under a name and
switch instantly (e.g. a strong **F1** profile, a softer **F2**, or a capped
**Kids** profile). Type a name and **Save As** to create one; **Save** overwrites
the active profile; **Delete** removes it. Your last-used profile is restored on
the next launch. Profiles are plain text files in the `profiles/` folder.

**Tune** Ã¢â‚¬â€ adjust the sliders live; changes apply instantly. Watch the
**CLIP** meter by the torque bar while driving:
- grey = healthy headroom
- yellow = occasional clipping
- red = clipping often (you're losing road detail Ã¢â‚¬â€ lower Overall Strength or
  raise Full-Scale Force)

---

## FFB Parameters

Most strength sliders are shown as a **percentage**. The two cue sliders go to
**200 %** so you can exaggerate them past the natural level.

| Parameter | What it does |
|-----------|--------------|
| **Overall Strength** | Master gain Ã¢â‚¬â€ multiplies *all* force evenly (scales). |
| **Max Output (Safety)** | Hard ceiling on *all* force, applied last (caps Ã¢â‚¬â€ only flattens peaks, doesn't scale). Set low for a kids profile Ã¢â‚¬â€ nothing can exceed it. |
| **Soft Start (s)** | Eases force in over this long after connecting/unpausing, so it never snaps on. |
| **Full-Scale Force (N)** | The in-game front-tyre force that fills the wheel to 100 % Ã¢â‚¬â€ a scaling reference, *not* a cap. **Higher = lighter** (more headroom before clipping); lower = stronger (clips sooner). Same convention as iRacing/AC max-force. |
| **Load Sensitivity** | *(Experimental)* Weights the wheel by the front tyres' vertical load Ã¢â‚¬â€ lighter when the front is unloaded (low speed, cresting a rise), heavier under high load (downforce at speed, trail-braking). **0 = off / original feel.** Only ever lightens, so it never adds clipping. |
| **Load Reference (N)** | Front vertical load treated as fully weighted (used by Load Sensitivity). Lower it if the wheel feels too light everywhere. |
| **Grip Loss Feel** | How strongly tyre slip reduces wheel force (>100 % = exaggerated). |
| **Understeer Cue** | Wheel lightening when the front tyres slide (>100 % = exaggerated). |
| **Oversteer Cue** | Counter-steer cue when the rear steps out (from vehicle sideslip). |
| **Lockup Judder** | Vibration when the front wheels lock under braking. |
| **Wheelspin Rumble** | Vibration when the rear wheels spin on power. |
| **Braking Weight** | Firms the wheel up under heavy braking load. |
| **Smoothing** | Output smoothing. A latency-vs-smoothness trade: higher = smoother but laggier (adds up to ~40 ms at max). Keep it low for the crispest feel; raise only as needed to calm DD-wheel oscillation. |
| **Min Force** | Lifts small forces above a wheel's mechanical deadzone. **Raise for belt/gear wheels** (Logitech, Thrustmaster); leave **0 for direct drive**. |
| **Update Rate (Hz)** | The *maximum* rate the wheel is updated. Output is phase-locked to each telemetry frame for lowest latency; this is the ceiling. See the note below. |
| **Invert Force Direction** | Flip the wheel's pull direction if forces feel reversed. |

### A note on Update Rate
The engine sends force the instant a fresh telemetry frame arrives (lowest
latency), and this slider is the **ceiling** Ã¢â‚¬â€ the fastest it will ever update
the wheel. **Many DirectInput wheels stop applying force, or even become
unstable, when updated too fast**, and that ceiling varies per wheel, so it's
left to you. **60Ã¢â‚¬â€œ90 Hz suits most wheels.** If you raise it and the force cuts
out or the app misbehaves, lower it again. (On the SimuCube 2 used for
development, the limit was around 245 Hz.)

---

## Safety features

This app is built around the fact that strong wheels can hurt you:

- **Pause / menu release** Ã¢â‚¬â€ if the game is paused, exited, in a menu, or
  minimised, the wheel is **released to zero** within a fraction of a second.
  The header shows **PAUSED Ã‚Â· FFB RELEASED**. Force returns smoothly when you
  resume (re-arm hysteresis prevents a stray menu frame from spiking the wheel).
- **Online-pause release** Ã¢â‚¬â€ in an online lobby the session keeps running while
  you're in the pause menu (the AI drives your car), so it isn't a normal pause.
  The app detects this and releases the wheel anyway, so it doesn't fight you
  while you navigate menus.
- **AI-takeover release** Ã¢â‚¬â€ when the AI takes over your car Ã¢â‚¬â€ at the end of a
  race / qualifying / session, or while being driven through the **pit lane** Ã¢â‚¬â€
  the wheel is released (header shows **AI DRIVING Ã‚Â· FFB RELEASED**). Force
  returns the moment you regain control (e.g. crossing the pit-exit line).
- **Soft start** Ã¢â‚¬â€ force always eases in rather than snapping on.
- **Max Output cap** Ã¢â‚¬â€ a hard ceiling you can set per profile (great for kids).
- **Speed fade** Ã¢â‚¬â€ no force below walking pace, so the pits stay calm.

> Ã¢Å¡Â  Direct-drive wheels are powerful. Start with a low **Overall Strength** /
> high **Max Force**, keep a hand ready, and raise gradually.

---

## How it works

```
F1 25 / 26  Ã¢â€â‚¬Ã¢â€â‚¬UDP 20777 (60 Hz)Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€“Âº  UdpReceiver  (parses motion, tyre forces,
                                                   slip, pedals, frame counter)
                                          Ã¢â€â€š  shared TelemetryState
                                          Ã¢â€“Â¼
                                     FFBEngine  (own thread, time-based loop)
                                          Ã¢â€â€š   base torque  = front lateral force
                                          Ã¢â€â€š   + understeer / oversteer cues
                                          Ã¢â€â€š   + lockup / wheelspin rumble
                                          Ã¢â€â€š   + braking weight
                                          Ã¢â€â€š   Ã¢â€ â€™ smoothing/interpolation
                                          Ã¢â€â€š   Ã¢â€ â€™ min-force, soft-start, max-output
                                          Ã¢â€“Â¼
                                     WheelOutput  (SDL2 haptic: constant force +
                                                   damper + sine effects)
                                          Ã¢â€“Â¼
                                     Your wheel base
```

## File structure
```
F125-Telemetry-FFB/
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ build.bat              Ã¢â€ Â build & run
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ release.ps1            Ã¢â€ Â one-command build + package + GitHub release (maintainer)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ CMakeLists.txt         Ã¢â€ Â fetches Dear ImGui + SDL2 (static)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ app.rc / F1FFB.ico     Ã¢â€ Â application icon
Ã¢â€â€Ã¢â€â‚¬Ã¢â€â‚¬ src/
    Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ main.cpp           Ã¢â€ Â Win32 window, D3D11, wiring, startup/shutdown
    Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ types.h            Ã¢â€ Â shared structs + tunables (FFBSettings)
    Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ udp_receiver.*     Ã¢â€ Â F1 UDP telemetry parser
    Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ ffb_engine.*       Ã¢â€ Â FFB signal processing
    Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ settings_io.*      Ã¢â€ Â profile save/load
    Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ output/
    Ã¢â€â€š   Ã¢â€â€Ã¢â€â‚¬Ã¢â€â‚¬ wheel_output.* Ã¢â€ Â SDL2 haptic FFB output
    Ã¢â€â€Ã¢â€â‚¬Ã¢â€â‚¬ gui/
        Ã¢â€â€Ã¢â€â‚¬Ã¢â€â‚¬ imgui_ui.*     Ã¢â€ Â Dear ImGui UI (Amber / Carbon theme)
```

---

## Troubleshooting

**Wheel won't connect** Ã¢â‚¬â€ close the wheel's tuning software and any game using
the wheel, then click the device again (the error message tells you why). Use
*Rescan Devices* if it isn't listed.

**Connected but no force** Ã¢â‚¬â€ make sure you're actually driving (no force when
paused, stationary, or on a straight, by design). Use *Send test force* to verify
the output path. If the wheel pulls the wrong way, tick **Invert Force Direction**.

**App crashes / force drops at high Update Rate** Ã¢â‚¬â€ lower the **Update Rate**;
your wheel's DirectInput driver can't keep up past a certain rate.

**Wheel feels weak** Ã¢â‚¬â€ lower **Full-Scale Force (N)** (lower = stronger) and/or
raise **Overall Strength**. Watch the CLIP meter so you don't over-drive it.

**Belt/gear wheel feels dead around centre** Ã¢â‚¬â€ raise **Min Force** until small
forces come alive (typically 0.05Ã¢â‚¬â€œ0.12).

---

## Support

F1 FFB is free and built in spare time. If it's improved your driving and you'd
like to support ongoing development, testing, and new features, you can leave a
tip Ã¢â‚¬â€ there's also a **Support on Ko-fi** button in the app:

Ã¢Ëœâ€¢ **[ko-fi.com/rapidbeno](https://ko-fi.com/rapidbeno)**

No pressure Ã¢â‚¬â€ thank you for driving with it either way!

---

## License

Released under the [MIT License](LICENSE) Ã¢â‚¬â€ free to use, modify, and
distribute. Provided **as-is, with no warranty**: direct-drive wheels are
powerful, and you use this software at your own risk.

Built with [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and
[SDL2](https://github.com/libsdl-org/SDL) (zlib).

---

*Built and tuned on a SimuCube 2, but vendor-neutral Ã¢â‚¬â€ it talks to any
DirectInput force-feedback wheel through SDL2.*
