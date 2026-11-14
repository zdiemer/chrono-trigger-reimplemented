/* The GBA's keypad as the SNES controller (keypad.h). */
#include "system/keypad.h"

#define KEYINPUT (*(volatile uint16_t *)0x04000130)

/* The keypad register's bits, each 0 while its key is held. */
enum {
    KEY_A = 1 << 0, KEY_B = 1 << 1, KEY_SELECT = 1 << 2, KEY_START = 1 << 3,
    KEY_RIGHT = 1 << 4, KEY_LEFT = 1 << 5, KEY_UP = 1 << 6, KEY_DOWN = 1 << 7,
    KEY_R = 1 << 8, KEY_L = 1 << 9, KEYS = 0x3FF,
};

uint16_t keypad_held(void) { return (uint16_t)(~KEYINPUT & KEYS); }

/* What a face key is held as: what it began as, chosen when it is pressed. */
static PadBits face(PadBits was, bool held, bool shifted, PadBits own, PadBits shift)
{
    if (!held)
        return 0;
    if (was)
        return was;
    return shifted ? shift : own;
}

PadBits keypad_buttons(Keypad *k, uint16_t held)
{
    static const struct { uint16_t key; PadBits button; } direct[] = {
        { KEY_SELECT, IN_BUTTON_SELECT }, { KEY_START, IN_BUTTON_START }, { KEY_RIGHT, IN_BUTTON_RIGHT },
        { KEY_LEFT, IN_BUTTON_LEFT },     { KEY_UP, IN_BUTTON_UP },       { KEY_DOWN, IN_BUTTON_DOWN },
    };
    bool shoulder = held & (KEY_L | KEY_R);
    k->a_as = face(k->a_as, held & KEY_A, shoulder, IN_BUTTON_A, IN_BUTTON_X);
    k->b_as = face(k->b_as, held & KEY_B, shoulder, IN_BUTTON_B, IN_BUTTON_Y);
    PadBits buttons = k->a_as | k->b_as;
    for (unsigned i = 0; i < sizeof direct / sizeof direct[0]; i++)
        if (held & direct[i].key)
            buttons |= direct[i].button;
    /* A shoulder is a shift while it makes an X or a Y, and a button otherwise. */
    if (!(buttons & (IN_BUTTON_X | IN_BUTTON_Y))) {
        if (held & KEY_L)
            buttons |= IN_BUTTON_L;
        if (held & KEY_R)
            buttons |= IN_BUTTON_R;
    }
    return buttons;
}
