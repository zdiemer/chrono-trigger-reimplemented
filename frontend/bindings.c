#include "bindings.h"

#include <ctype.h>
#include <string.h>

#include "movie.h"

/* A stick or trigger counts as pressed past half way. */
#define AXIS_THRESHOLD 16384
#define MAX_BINDINGS 128

typedef enum { IN_KEY, IN_PAD_BUTTON, IN_PAD_AXIS } InputKind;

typedef struct {
    InputKind kind;
    int code;   /* SDL_Scancode, SDL_GameControllerButton or SDL_GameControllerAxis */
    int sign;   /* axes: +1 or -1 */
} Input;

typedef struct {
    Input in;
    Action action;
    uint16_t button;   /* ACT_BUTTON */
} Binding;

static Binding table[MAX_BINDINGS];
static int count;

static const struct {
    const char *name;
    uint16_t button;
} buttons[] = {
    { "Up", CT_BTN_UP }, { "Down", CT_BTN_DOWN }, { "Left", CT_BTN_LEFT }, { "Right", CT_BTN_RIGHT },
    { "A", CT_BTN_A }, { "B", CT_BTN_B }, { "X", CT_BTN_X }, { "Y", CT_BTN_Y },
    { "L", CT_BTN_L }, { "R", CT_BTN_R }, { "Start", CT_BTN_START }, { "Select", CT_BTN_SELECT },
};
#define BUTTON_COUNT (int)(sizeof buttons / sizeof buttons[0])

static const char *const action_names[ACT_COUNT] = {
    [ACT_NONE] = "None", [ACT_PAUSE] = "Pause", [ACT_FAST_FORWARD] = "FastForward",
    [ACT_QUICK_SAVE] = "QuickSave", [ACT_QUICK_LOAD] = "QuickLoad", [ACT_FULLSCREEN] = "Fullscreen",
    [ACT_QUIT] = "Quit",
};

static const char *const defaults[] = {
    /* The keyboard ct_play always had. */
    "Up = Up", "Down = Down", "Left = Left", "Right = Right",
    "X = A", "Z = B", "S = X", "A = Y", "Q = L", "W = R", "Return = Start", "Right Shift = Select",
    "P = Pause", "Pause = Pause", "Tab = FastForward", "F5 = QuickSave", "F7 = QuickLoad", "F11 = Fullscreen",
    "Escape = Quit",
    /* Pads by position, as on a SNES pad: SDL names the bottom face button a
     * (a SNES's B), the right one b (A), the left one x (Y), the top one y (X). */
    "Pad dpup = Up", "Pad dpdown = Down", "Pad dpleft = Left", "Pad dpright = Right",
    "Pad lefty- = Up", "Pad lefty+ = Down", "Pad leftx- = Left", "Pad leftx+ = Right",
    "Pad a = B", "Pad b = A", "Pad x = Y", "Pad y = X",
    "Pad leftshoulder = L", "Pad rightshoulder = R", "Pad start = Start", "Pad back = Select",
    "Pad righttrigger = FastForward",
};

static bool same_input(Input a, Input b) { return a.kind == b.kind && a.code == b.code && a.sign == b.sign; }

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    size_t lead = strspn(s, " \t");
    memmove(s, s + lead, n - lead + 1);
}

static bool starts_with_pad(const char *s)
{
    return (s[0] == 'P' || s[0] == 'p') && (s[1] == 'A' || s[1] == 'a') && (s[2] == 'D' || s[2] == 'd') && s[3] == ' ';
}

static int parse_input(const char *s, Input *in)
{
    if (starts_with_pad(s)) {
        char name[128];
        snprintf(name, sizeof name, "%s", s + 4);
        trim(name);
        SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(name);
        if (b != SDL_CONTROLLER_BUTTON_INVALID) {
            *in = (Input){ IN_PAD_BUTTON, b, 0 };
            return 0;
        }
        size_t n = strlen(name);
        int sign = 1;
        if (n && (name[n - 1] == '+' || name[n - 1] == '-')) {
            sign = name[n - 1] == '-' ? -1 : 1;
            name[n - 1] = 0;
        }
        SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(name);
        if (a == SDL_CONTROLLER_AXIS_INVALID)
            return -1;
        *in = (Input){ IN_PAD_AXIS, a, sign };
        return 0;
    }
    const char *name = s;
    if (!SDL_strcasecmp(s, "Enter"))
        name = "Return";
    else if (!SDL_strcasecmp(s, "Esc"))
        name = "Escape";
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    if (sc == SDL_SCANCODE_UNKNOWN)
        return -1;
    *in = (Input){ IN_KEY, sc, 0 };
    return 0;
}

static int parse_action(const char *s, Action *action, uint16_t *button)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!SDL_strcasecmp(s, buttons[i].name)) {
            *action = ACT_BUTTON;
            *button = buttons[i].button;
            return 0;
        }
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        if (action_names[a] && !SDL_strcasecmp(s, action_names[a])) {
            *action = (Action)a;
            *button = 0;
            return 0;
        }
    }
    return -1;
}

int bindings_set(const char *spec, char *err, size_t err_len)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s", spec);
    char *eq = strrchr(buf, '=');
    if (!eq || eq == buf) {
        snprintf(err, err_len, "expected INPUT = ACTION");
        return -1;
    }
    *eq = 0;
    char *action_name = eq + 1;
    trim(buf);
    trim(action_name);
    Input in;
    Action action;
    uint16_t button;
    if (parse_input(buf, &in) != 0) {
        snprintf(err, err_len, "no key or pad input called \"%s\"", buf);
        return -1;
    }
    if (parse_action(action_name, &action, &button) != 0) {
        snprintf(err, err_len, "no button or action called \"%s\"", action_name);
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (same_input(table[i].in, in)) {
            memmove(&table[i], &table[i + 1], (size_t)(count - i - 1) * sizeof table[0]);
            count--;
            break;
        }
    }
    if (action == ACT_NONE)
        return 0;
    if (count == MAX_BINDINGS) {
        snprintf(err, err_len, "more than %d bindings", MAX_BINDINGS);
        return -1;
    }
    /* Keep the order bindings were made in, for --help. */
    table[count++] = (Binding){ in, action, button };
    return 0;
}

void bindings_reset(void)
{
    count = 0;
    char err[128];
    for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
        bindings_set(defaults[i], err, sizeof err);
}

int bindings_load(const char *text, size_t len, const char *source)
{
    int bad = 0, line_no = 0;
    size_t at = 0;
    while (at < len) {
        char line[256];
        size_t n = 0;
        while (at < len && text[at] != '\n') {
            if (n < sizeof line - 1)
                line[n++] = text[at];
            at++;
        }
        at++;
        line[n] = 0;
        line_no++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        trim(line);
        if (!line[0])
            continue;
        char err[160];
        if (bindings_set(line, err, sizeof err) != 0) {
            fprintf(stderr, "%s:%d: %s\n", source, line_no, err);
            bad++;
        }
    }
    return bad;
}

static void input_name(Input in, char *out, size_t n)
{
    switch (in.kind) {
    case IN_KEY:
        snprintf(out, n, "%s", SDL_GetScancodeName((SDL_Scancode)in.code));
        break;
    case IN_PAD_BUTTON:
        snprintf(out, n, "Pad %s", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)in.code));
        break;
    case IN_PAD_AXIS: {
        SDL_GameControllerAxis a = (SDL_GameControllerAxis)in.code;
        bool trigger = a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        snprintf(out, n, "Pad %s%s", SDL_GameControllerGetStringForAxis(a), trigger ? "" : in.sign < 0 ? "-" : "+");
        break;
    }
    }
}

const char *bindings_name(Action action, char *out, size_t n, const char *fallback)
{
    for (int i = 0; i < count; i++) {
        if (table[i].action == action && table[i].in.kind == IN_KEY) {
            input_name(table[i].in, out, n);
            return out;
        }
    }
    for (int i = 0; i < count; i++) {
        if (table[i].action == action) {
            input_name(table[i].in, out, n);
            return out;
        }
    }
    return fallback;
}

static void print_row(FILE *out, const char *label, Action action, uint16_t button, const char *extra)
{
    char row[512] = "";
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        if (table[i].action != action || (action == ACT_BUTTON && table[i].button != button))
            continue;
        char name[64];
        input_name(table[i].in, name, sizeof name);
        len += (size_t)snprintf(row + len, sizeof row - len, "%s%s", len ? ", " : "", name);
        if (len >= sizeof row)
            break;
    }
    if (extra)
        snprintf(row + len, sizeof row - len, "%s%s", len ? ", " : "", extra);
    fprintf(out, "  %-13s %s\n", label, row[0] ? row : "(none)");
}

void bindings_print(FILE *out)
{
    for (int i = 0; i < BUTTON_COUNT; i++)
        print_row(out, buttons[i].name, ACT_BUTTON, buttons[i].button, NULL);
    print_row(out, "pause", ACT_PAUSE, 0, NULL);
    print_row(out, "fast forward", ACT_FAST_FORWARD, 0, NULL);
    print_row(out, "quick save", ACT_QUICK_SAVE, 0, NULL);
    print_row(out, "quick load", ACT_QUICK_LOAD, 0, NULL);
    print_row(out, "fullscreen", ACT_FULLSCREEN, 0, "Alt+Return");
    print_row(out, "quit", ACT_QUIT, 0, NULL);
}

static bool pad_held(Input in, SDL_GameController *const *pads, int npads)
{
    for (int p = 0; p < npads; p++) {
        if (in.kind == IN_PAD_BUTTON && SDL_GameControllerGetButton(pads[p], (SDL_GameControllerButton)in.code))
            return true;
        if (in.kind == IN_PAD_AXIS &&
            SDL_GameControllerGetAxis(pads[p], (SDL_GameControllerAxis)in.code) * in.sign >= AXIS_THRESHOLD)
            return true;
    }
    return false;
}

static bool held(const Binding *b, const Uint8 *keys, SDL_GameController *const *pads, int npads)
{
    if (b->in.kind == IN_KEY)
        return keys && keys[b->in.code];
    return pad_held(b->in, pads, npads);
}

uint16_t bindings_buttons(SDL_GameController *const *pads, int npads)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    /* Alt+Return toggles fullscreen; don't also press what Return is bound to. */
    bool alt = (SDL_GetModState() & KMOD_ALT) != 0;
    uint16_t pad = 0;
    for (int i = 0; i < count; i++) {
        const Binding *b = &table[i];
        if (b->action != ACT_BUTTON || (alt && b->in.kind == IN_KEY && b->in.code == SDL_SCANCODE_RETURN))
            continue;
        if (held(b, keys, pads, npads))
            pad |= b->button;
    }
    /* A real d-pad can't press opposite directions together, and games don't expect it. */
    if ((pad & (CT_BTN_UP | CT_BTN_DOWN)) == (CT_BTN_UP | CT_BTN_DOWN))
        pad &= (uint16_t)~(CT_BTN_UP | CT_BTN_DOWN);
    if ((pad & (CT_BTN_LEFT | CT_BTN_RIGHT)) == (CT_BTN_LEFT | CT_BTN_RIGHT))
        pad &= (uint16_t)~(CT_BTN_LEFT | CT_BTN_RIGHT);
    return pad;
}

Action bindings_key_action(SDL_Scancode key)
{
    for (int i = 0; i < count; i++) {
        if (table[i].in.kind == IN_KEY && table[i].in.code == (int)key)
            return table[i].action;
    }
    return ACT_NONE;
}

bool bindings_held(Action action, SDL_GameController *const *pads, int npads, bool keyboard)
{
    const Uint8 *keys = keyboard ? SDL_GetKeyboardState(NULL) : NULL;
    for (int i = 0; i < count; i++) {
        if (table[i].action == action && held(&table[i], keys, pads, npads))
            return true;
    }
    return false;
}
