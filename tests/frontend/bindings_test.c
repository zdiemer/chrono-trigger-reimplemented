/* Checks ct_play's gamepad and key bindings (frontend/bindings.c) on a virtual
 * SDL game controller: the default layout by position (the bottom face button
 * is the SNES's B), the left stick and its dead zone, opposite directions
 * cancelling, a trigger held for fast forward, rebinding, and bad bindings
 * refused. Skips (exit 0) where SDL can't make a virtual controller. Exits
 * non-zero on failure.
 *
 *   bindings_test */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>

#include "bindings.h"
#include "movie.h"

static int failures;

#define CHECK(cond, what)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    SDL_SetMainReady();
    char err[160];
    bindings_reset();
    CHECK(bindings_set("Foo = A", err, sizeof err) != 0, "an unknown key binds");
    CHECK(bindings_set("X = Jump", err, sizeof err) != 0, "an unknown action binds");
    CHECK(bindings_set("Pad nothing = A", err, sizeof err) != 0, "an unknown pad input binds");
    CHECK(bindings_set("X", err, sizeof err) != 0, "a binding without = binds");
    CHECK(bindings_key_action(SDL_SCANCODE_F5) == ACT_QUICK_SAVE, "F5 isn't quick save by default");
    CHECK(bindings_set("F5 = None", err, sizeof err) == 0 && bindings_key_action(SDL_SCANCODE_F5) == ACT_NONE,
          "F5 = None doesn't unbind");
    CHECK(bindings_set("enter = QuickLoad", err, sizeof err) == 0 &&
              bindings_key_action(SDL_SCANCODE_RETURN) == ACT_QUICK_LOAD,
          "Enter doesn't name Return");
    const char keys[] = "# a key file\nSpace = Start   # comment\n\nPad x = Quit\n";
    CHECK(bindings_load(keys, sizeof keys - 1, "test") == 0, "a key file doesn't load");
    CHECK(bindings_key_action(SDL_SCANCODE_SPACE) == ACT_BUTTON, "Space isn't bound by the key file");
    bindings_reset();

#if SDL_VERSION_ATLEAST(2, 24, 0)
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        printf("bindings_test: no game controller subsystem (%s); pad checks skipped\n", SDL_GetError());
        return failures != 0;
    }
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "bindings_test pad";
    int device = SDL_JoystickAttachVirtualEx(&desc);
    SDL_GameController *pad = device >= 0 ? SDL_GameControllerOpen(device) : NULL;
    if (!pad) {
        printf("bindings_test: no virtual controller (%s); pad checks skipped\n", SDL_GetError());
        SDL_Quit();
        return failures != 0;
    }
    SDL_Joystick *j = SDL_GameControllerGetJoystick(pad);
    SDL_GameController *pads[1] = { pad };
#define BUTTON(b, down) SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_##b, down)
#define AXIS(a, v) SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_##a, v)
#define NOW() (SDL_GameControllerUpdate(), bindings_buttons(pads, 1))

    BUTTON(A, 1);
    CHECK(NOW() == CT_BTN_B, "the bottom button isn't B");
    BUTTON(A, 0), BUTTON(B, 1);
    CHECK(NOW() == CT_BTN_A, "the right button isn't A");
    BUTTON(B, 0), BUTTON(X, 1);
    CHECK(NOW() == CT_BTN_Y, "the left button isn't Y");
    BUTTON(X, 0), BUTTON(Y, 1);
    CHECK(NOW() == CT_BTN_X, "the top button isn't X");
    BUTTON(Y, 0), BUTTON(START, 1), BUTTON(BACK, 1), BUTTON(LEFTSHOULDER, 1), BUTTON(RIGHTSHOULDER, 1);
    CHECK(NOW() == (CT_BTN_START | CT_BTN_SELECT | CT_BTN_L | CT_BTN_R), "start, back and shoulders");
    BUTTON(START, 0), BUTTON(BACK, 0), BUTTON(LEFTSHOULDER, 0), BUTTON(RIGHTSHOULDER, 0), BUTTON(DPAD_UP, 1);
    CHECK(NOW() == CT_BTN_UP, "the d-pad isn't the d-pad");
    BUTTON(DPAD_UP, 0), AXIS(LEFTY, 30000);
    CHECK(NOW() == CT_BTN_DOWN, "the left stick down isn't Down");
    AXIS(LEFTY, 8000);
    CHECK(NOW() == 0, "the left stick's dead zone presses a button");
    AXIS(LEFTY, 0), BUTTON(DPAD_LEFT, 1), AXIS(LEFTX, 30000);
    CHECK(NOW() == 0, "left and right together don't cancel");
    BUTTON(DPAD_LEFT, 0), AXIS(LEFTX, 0), AXIS(TRIGGERRIGHT, 32767);
    SDL_GameControllerUpdate();
    CHECK(bindings_held(ACT_FAST_FORWARD, pads, 1, false), "the right trigger isn't fast forward");
    AXIS(TRIGGERRIGHT, 0);
    SDL_GameControllerUpdate();
    CHECK(!bindings_held(ACT_FAST_FORWARD, pads, 1, false), "fast forward stays held");

    CHECK(bindings_set("Pad guide = QuickSave", err, sizeof err) == 0, "Pad guide doesn't bind");
    CHECK(bindings_set("Pad a = A", err, sizeof err) == 0, "Pad a doesn't rebind");
    BUTTON(GUIDE, 1), BUTTON(A, 1);
    CHECK(NOW() == CT_BTN_A, "the rebound bottom button isn't A");
    CHECK(bindings_held(ACT_QUICK_SAVE, pads, 1, false), "guide isn't quick save");
    SDL_GameControllerClose(pad);
    SDL_JoystickDetachVirtual(device);
    SDL_Quit();
#else
    printf("bindings_test: SDL before 2.24 has no virtual controllers; pad checks skipped\n");
#endif
    if (failures) {
        printf("bindings_test: %d failures\n", failures);
        return 1;
    }
    printf("bindings_test: all checks passed\n");
    return 0;
}
