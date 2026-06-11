// Standalone haptic diagnostic. Lists devices, prints which effect types the
// wheel accepts, then plays three tests so wheel-side rendering can be checked
// independent of the main app:
//   1. constant-force pulse  (the channel known to work — sanity check)
//   2. sine rumble, run directly at magnitude
//   3. sine rumble via create-at-zero + update-while-playing (the app's pattern)
// If 2 buzzes but 3 doesn't, the driver ignores magnitude updates to a playing
// periodic effect and the app must restart the effect instead.
//
// Build:  cmake --build build --config Release --target HapticProbe
// Run:    build\Release\HapticProbe.exe   (close F1FFB + tuning software first)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>

static void cap(unsigned q, unsigned bit, const char* name) {
    printf("  %-11s %s\n", name, (q & bit) ? "YES" : "no");
}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int n = SDL_NumHaptics();
    printf("Haptic devices: %d\n", n);
    for (int i = 0; i < n; i++)
        printf("  [%d] %s\n", i, SDL_HapticName(i));
    if (n == 0) {
        printf("No devices found. Is the wheel powered on?\n");
        printf("Press Enter to exit."); getchar();
        return 0;
    }

    int idx = (argc > 1) ? atoi(argv[1]) : 0;
    printf("\nOpening device %d (pass a number argument to pick another)...\n", idx);
    SDL_Haptic* h = SDL_HapticOpen(idx);
    if (!h) {
        printf("Open FAILED: %s\n", SDL_GetError());
        printf("Close F1FFB, any game, and the wheel's tuning software, then retry.\n");
        printf("Press Enter to exit."); getchar();
        return 1;
    }

    unsigned q = SDL_HapticQuery(h);
    printf("\nEffect support reported by the driver (mask 0x%08X):\n", q);
    cap(q, SDL_HAPTIC_CONSTANT,   "CONSTANT");
    cap(q, SDL_HAPTIC_SINE,       "SINE");
    cap(q, SDL_HAPTIC_TRIANGLE,   "TRIANGLE");
    cap(q, SDL_HAPTIC_SAWTOOTHUP, "SAWTOOTH");
    cap(q, SDL_HAPTIC_SPRING,     "SPRING");
    cap(q, SDL_HAPTIC_DAMPER,     "DAMPER");
    cap(q, SDL_HAPTIC_FRICTION,   "FRICTION");
    cap(q, SDL_HAPTIC_GAIN,       "GAIN");
    cap(q, SDL_HAPTIC_AUTOCENTER, "AUTOCENTER");
    printf("Effect slots: %d (max playing: %d)\n",
           SDL_HapticNumEffects(h), SDL_HapticNumEffectsPlaying(h));

    SDL_HapticSetAutocenter(h, 0);
    SDL_HapticSetGain(h, 100);

    printf("\nHold the wheel LIGHTLY. Starting tests...\n");

    // [1] Constant-force pulse — proves the probe is talking to the wheel.
    printf("\n[1/3] CONSTANT pulse, 15%% for 1s...\n");
    SDL_HapticEffect ce{};
    ce.type                    = SDL_HAPTIC_CONSTANT;
    ce.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    ce.constant.direction.dir[0] = 1;
    ce.constant.length         = 1000;
    ce.constant.level          = (Sint16)(32767 * 0.15f);
    int cid = SDL_HapticNewEffect(h, &ce);
    if (cid < 0) printf("  create FAILED: %s\n", SDL_GetError());
    else {
        if (SDL_HapticRunEffect(h, cid, 1) < 0) printf("  run FAILED: %s\n", SDL_GetError());
        else printf("  sent OK\n");
        SDL_Delay(1500);
        SDL_HapticDestroyEffect(h, cid);
    }

    // [2] Sine run directly at magnitude — simplest possible rumble.
    printf("[2/3] SINE rumble, 50%% at 28Hz for 3s (run directly)...\n");
    SDL_HapticEffect se{};
    se.type                    = SDL_HAPTIC_SINE;
    se.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
    se.periodic.direction.dir[0] = 1;
    se.periodic.period         = 36;
    se.periodic.magnitude      = (Sint16)(32767 * 0.5f);
    se.periodic.length         = 3000;
    int sid = SDL_HapticNewEffect(h, &se);
    if (sid < 0) printf("  create FAILED: %s\n", SDL_GetError());
    else {
        if (SDL_HapticRunEffect(h, sid, 1) < 0) printf("  run FAILED: %s\n", SDL_GetError());
        else printf("  sent OK\n");
        SDL_Delay(3500);
        SDL_HapticDestroyEffect(h, sid);
    }

    // [3] Sine the way the APP does it: infinite effect created at magnitude 0,
    // started, then the magnitude updated while it plays.
    printf("[3/3] SINE rumble via update-while-playing (app's pattern), 3s...\n");
    SDL_HapticEffect ue{};
    ue.type                    = SDL_HAPTIC_SINE;
    ue.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
    ue.periodic.direction.dir[0] = 1;
    ue.periodic.period         = 36;
    ue.periodic.magnitude      = 0;
    ue.periodic.length         = SDL_HAPTIC_INFINITY;
    int uid = SDL_HapticNewEffect(h, &ue);
    if (uid < 0) printf("  create FAILED: %s\n", SDL_GetError());
    else {
        if (SDL_HapticRunEffect(h, uid, 1) < 0) printf("  run FAILED: %s\n", SDL_GetError());
        ue.periodic.magnitude = (Sint16)(32767 * 0.5f);
        if (SDL_HapticUpdateEffect(h, uid, &ue) < 0) printf("  update FAILED: %s\n", SDL_GetError());
        else printf("  sent OK\n");
        SDL_Delay(3000);
        ue.periodic.magnitude = 0;
        SDL_HapticUpdateEffect(h, uid, &ue);
        SDL_HapticDestroyEffect(h, uid);
    }

    SDL_HapticClose(h);
    SDL_Quit();

    printf("\nDone. Note which tests you FELT (1 / 2 / 3) and what printed above.\n");
    printf("Press Enter to exit.");
    getchar();
    return 0;
}
