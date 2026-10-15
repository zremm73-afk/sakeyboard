// GTA San Andreas Android 2.10 (arm64-v8a) - keyboard controls with remapping.
//
// How it works
//  * Hooks the game's JNI key-event handler to learn which keys are held.
//  * Hooks CPad getters (GetPedWalkLeftRight, GetJump, ...). While a bound key
//    is held the getter returns the keyboard value; otherwise the original
//    (touch / gamepad) value is returned, so both input methods keep working.
//  * Key bindings live in keyboard_map.ini. Press F12 in game to reload it.
//
// Every hook target is checked against a 16-byte fingerprint of YOUR 2.10 build
// (hooks_table.h). A mismatch means the hook is skipped, not applied blindly.

#include <mod/amlmod.h>
#include <android/log.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

MYMOD(net.example.gtasa.keyboard, SA Keyboard, 1.0, you)

#define TAG "SAKeyboard"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static const char *CONFIG_PATH =
    "/sdcard/Android/data/com.rockstargames.gtasa/files/keyboard_map.ini";

// ------------------------------------------------------------------ actions
enum Action {
    A_WALK_FWD, A_WALK_BACK, A_WALK_LEFT, A_WALK_RIGHT,
    A_CAM_UP, A_CAM_DOWN, A_CAM_LEFT, A_CAM_RIGHT,
    A_JUMP, A_SPRINT, A_ENTER_EXIT, A_CROUCH, A_FIRE, A_AIM,
    A_ACCELERATE, A_BRAKE, A_HANDBRAKE, A_HORN,
    A_WEAPON_PREV, A_WEAPON_NEXT, A_LOOK_BEHIND, A_YES, A_NO,
    A_COUNT
};

// Names used in keyboard_map.ini (same order as the enum).
static const char *ACTION_NAMES[A_COUNT] = {
    "walk_forward", "walk_back", "walk_left", "walk_right",
    "camera_up", "camera_down", "camera_left", "camera_right",
    "jump", "sprint", "enter_exit_vehicle", "crouch", "fire", "aim",
    "accelerate", "brake", "handbrake", "horn",
    "weapon_prev", "weapon_next", "look_behind", "yes", "no",
};

#include "hooks_table.h"   // needs the Action enum above

// ------------------------------------------------------------- key bindings
static const int MAX_KEYS_PER_ACTION = 4;
static std::atomic<int> gBind[A_COUNT][MAX_KEYS_PER_ACTION];

static const int MAX_KEYCODE = 512;
static std::atomic<bool> gKeyDown[MAX_KEYCODE];
static std::atomic<bool> gPending[A_COUNT];   // key went down since last frame
static std::atomic<bool> gLatched[A_COUNT];   // visible to *JustDown getters

static int  gCamSpeed   = 64;
static bool gInvertX    = false;
static bool gInvertY    = false;
static bool gLogKeys    = false;

static const int KEY_F12 = 142;   // reloads the config

// ---------------------------------------------------------- key name lookup
struct KeyName { const char *name; int code; };
static const KeyName KEY_NAMES[] = {
    {"SPACE", 62}, {"ENTER", 66}, {"TAB", 61}, {"ESC", 111}, {"BACKSPACE", 67},
    {"SHIFT_LEFT", 59}, {"SHIFT_RIGHT", 60}, {"CTRL_LEFT", 113}, {"CTRL_RIGHT", 114},
    {"ALT_LEFT", 57}, {"ALT_RIGHT", 58}, {"CAPS_LOCK", 115},
    {"UP", 19}, {"DOWN", 20}, {"LEFT", 21}, {"RIGHT", 22},
    {"DPAD_UP", 19}, {"DPAD_DOWN", 20}, {"DPAD_LEFT", 21}, {"DPAD_RIGHT", 22},
    {"PAGE_UP", 92}, {"PAGE_DOWN", 93}, {"HOME", 122}, {"END", 123},
    {"INSERT", 124}, {"DELETE", 112},
    {"GRAVE", 68}, {"MINUS", 69}, {"EQUALS", 70}, {"LEFT_BRACKET", 71},
    {"RIGHT_BRACKET", 72}, {"BACKSLASH", 73}, {"SEMICOLON", 74},
    {"APOSTROPHE", 75}, {"SLASH", 76}, {"COMMA", 55}, {"PERIOD", 56},
};

static int KeyFromName(std::string s) {
    for (auto &c : s) c = (char)toupper((unsigned char)c);
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    if (s.empty()) return -1;
    if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'Z') return 29 + (s[0] - 'A');
    if (s.size() == 1 && s[0] >= '0' && s[0] <= '9') return 7 + (s[0] - '0');
    if (s[0] == 'F' && s.size() <= 3) {                       // F1..F12
        int n = atoi(s.c_str() + 1);
        if (n >= 1 && n <= 12) return 131 + (n - 1);
    }
    if (s.rfind("NUMPAD", 0) == 0 && s.size() == 7 && isdigit((unsigned char)s[6]))
        return 144 + (s[6] - '0');
    for (const auto &k : KEY_NAMES) if (s == k.name) return k.code;
    char *end = nullptr;                                       // raw Android keycode
    long v = strtol(s.c_str(), &end, 10);
    if (end && *end == 0 && v > 0 && v < MAX_KEYCODE) return (int)v;
    return -1;
}

// -------------------------------------------------------------- config file
static const char *DEFAULT_INI =
    "# GTA SA keyboard controls. One action per line: action=KEY1,KEY2\n"
    "# Up to 4 keys per action. Press F12 in game to reload this file.\n"
    "# Key names: A-Z, 0-9, F1-F12, SPACE, ENTER, TAB, ESC, SHIFT_LEFT,\n"
    "# SHIFT_RIGHT, CTRL_LEFT, CTRL_RIGHT, ALT_LEFT, ALT_RIGHT, UP, DOWN,\n"
    "# LEFT, RIGHT, PAGE_UP, PAGE_DOWN, HOME, END, NUMPAD0-9, COMMA, PERIOD...\n"
    "# or a raw Android keycode number. Set log_keys=1 to print pressed keys\n"
    "# to logcat (tag SAKeyboard).\n"
    "\n"
    "[options]\n"
    "camera_speed=64\n"
    "camera_invert_x=0\n"
    "camera_invert_y=0\n"
    "log_keys=0\n"
    "\n"
    "[on foot + vehicle]\n"
    "walk_forward=W\n"
    "walk_back=S\n"
    "walk_left=A\n"
    "walk_right=D\n"
    "camera_up=UP\n"
    "camera_down=DOWN\n"
    "camera_left=LEFT\n"
    "camera_right=RIGHT\n"
    "jump=SPACE\n"
    "sprint=SHIFT_LEFT\n"
    "enter_exit_vehicle=F\n"
    "crouch=C\n"
    "fire=CTRL_LEFT\n"
    "aim=ALT_LEFT\n"
    "weapon_prev=Q\n"
    "weapon_next=E\n"
    "yes=Y\n"
    "no=N\n"
    "\n"
    "[vehicle]\n"
    "accelerate=W\n"
    "brake=S\n"
    "handbrake=SPACE\n"
    "horn=H\n"
    "look_behind=B\n";

static void SetDefaultsFromText(const char *text);

static void WriteDefaultIfMissing() {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (f) { fclose(f); return; }
    f = fopen(CONFIG_PATH, "w");
    if (!f) { LOGE("cannot create %s", CONFIG_PATH); return; }
    fputs(DEFAULT_INI, f);
    fclose(f);
    LOGI("wrote default config to %s", CONFIG_PATH);
}

static void ApplyLine(std::string line) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    size_t eq = line.find('=');
    if (eq == std::string::npos) return;                       // sections, blanks
    std::string key = line.substr(0, eq), val = line.substr(eq + 1);
    for (auto &c : key) c = (char)tolower((unsigned char)c);
    while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
    while (!key.empty() && isspace((unsigned char)key.front())) key.erase(key.begin());

    if (key == "camera_speed")    { gCamSpeed = atoi(val.c_str()); return; }
    if (key == "camera_invert_x") { gInvertX = atoi(val.c_str()) != 0; return; }
    if (key == "camera_invert_y") { gInvertY = atoi(val.c_str()) != 0; return; }
    if (key == "log_keys")        { gLogKeys = atoi(val.c_str()) != 0; return; }

    for (int a = 0; a < A_COUNT; a++) {
        if (key != ACTION_NAMES[a]) continue;
        int slot = 0;
        for (int i = 0; i < MAX_KEYS_PER_ACTION; i++) gBind[a][i] = -1;
        size_t start = 0;
        while (start <= val.size() && slot < MAX_KEYS_PER_ACTION) {
            size_t comma = val.find(',', start);
            std::string tok = val.substr(start, comma == std::string::npos ? comma : comma - start);
            int code = KeyFromName(tok);
            if (code >= 0) gBind[a][slot++] = code;
            else if (!tok.empty()) LOGE("unknown key '%s' for %s", tok.c_str(), ACTION_NAMES[a]);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return;
    }
    LOGE("unknown setting '%s'", key.c_str());
}

static void SetDefaultsFromText(const char *text) {
    std::string cur;
    for (const char *p = text; ; p++) {
        if (*p == '\n' || *p == 0) { ApplyLine(cur); cur.clear(); if (!*p) break; }
        else if (*p != '\r') cur += *p;
    }
}

static void LoadConfig() {
    for (int a = 0; a < A_COUNT; a++)
        for (int i = 0; i < MAX_KEYS_PER_ACTION; i++) gBind[a][i] = -1;
    SetDefaultsFromText(DEFAULT_INI);                          // baseline
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) { LOGE("no config, using defaults"); return; }
    char buf[512];
    while (fgets(buf, sizeof buf, f)) ApplyLine(buf);
    fclose(f);
    LOGI("config loaded");
}

// --------------------------------------------------------------- key state
static bool IsHeld(int action) {
    for (int i = 0; i < MAX_KEYS_PER_ACTION; i++) {
        int k = gBind[action][i];
        if (k >= 0 && k < MAX_KEYCODE && gKeyDown[k]) return true;
    }
    return false;
}

static void OnKey(int keycode, bool down) {
    if (keycode < 0 || keycode >= MAX_KEYCODE) return;
    if (gLogKeys) LOGI("key %d %s", keycode, down ? "down" : "up");
    bool was = gKeyDown[keycode].exchange(down);
    if (!down || was) return;                                  // edge = fresh press only
    if (keycode == KEY_F12) { LoadConfig(); return; }
    for (int a = 0; a < A_COUNT; a++)
        for (int i = 0; i < MAX_KEYS_PER_ACTION; i++)
            if (gBind[a][i] == keycode) gPending[a] = true;
}

// ------------------------------------------------------------------- hooks
static void (*orig_KeyEvent)(void *, void *, int, int, int, int) = nullptr;
static void hook_KeyEvent(void *env, void *obj, int a, int b, int c, int d) {
    // Expected order: (action, keycode, unicode, meta). Action is 0 (down) or
    // 1 (up). If the first value doesn't look like an action but the second
    // does, assume the order is swapped.
    int action = a, key = b;
    if ((a != 0 && a != 1) && (b == 0 || b == 1)) { action = b; key = a; }
    OnKey(key, action == 0);
    orig_KeyEvent(env, obj, a, b, c, d);
}

static void (*orig_UpdatePads)(void *) = nullptr;
static void hook_UpdatePads(void *self) {
    for (int a = 0; a < A_COUNT; a++) gLatched[a] = gPending[a].exchange(false);
    orig_UpdatePads(self);
}

typedef uint64_t (*GenericFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static GenericFn gOrig[HOOK_COUNT];

// One generic hook per entry. Forwarding x0..x5 untouched keeps the original
// call intact whatever its real parameter list is (all are integer/pointer args).
template <int I>
static uint64_t GenericHook(uint64_t a0, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5) {
    const HookDef &h = HOOKS[I];
    switch (h.kind) {
    case K_AXIS: {
        int pos = IsHeld(h.a) ? 1 : 0, neg = IsHeld(h.b) ? 1 : 0;
        int dir = pos - neg;
        if (dir != 0) {
            int mag = h.value > 0 ? h.value : gCamSpeed;       // camera uses its own speed
            if (h.value < 0) {                                  // camera axes can be inverted
                bool horizontal = (h.a == A_CAM_RIGHT);
                if (horizontal ? gInvertX : gInvertY) dir = -dir;
            }
            return (uint64_t)(int64_t)(dir * mag);
        }
        break;
    }
    case K_HELD:
        if (IsHeld(h.a)) return (uint64_t)h.value;
        break;
    case K_EDGE:
        if (gLatched[h.a]) return (uint64_t)h.value;
        break;
    }
    return gOrig[I](a0, a1, a2, a3, a4, a5);
}

static bool Verify(uintptr_t base, const Fingerprint &fp) {
    return memcmp((const void *)(base + fp.off), fp.bytes, sizeof fp.bytes) == 0;
}

template <int I>
static void InstallOne(uintptr_t base) {
    const HookDef &h = HOOKS[I];
    if (!Verify(base, h.fp)) { LOGE("skip %s: fingerprint mismatch", h.name); return; }
    aml->Hook((void *)(base + h.fp.off), (void *)&GenericHook<I>, (void **)&gOrig[I]);
}

template <int... Is>
static void InstallAll(uintptr_t base, std::integer_sequence<int, Is...>) {
    (InstallOne<Is>(base), ...);
}

// ------------------------------------------------------------------ loader
ON_MOD_PRELOAD() {
    uintptr_t base = aml->GetLib("libGTASA.so");
    if (!base) { LOGE("libGTASA.so not found"); return; }

    // Refuse to run on any build other than the one the table was made from.
    if (!Verify(base, FP_KeyEvent) || !Verify(base, FP_UpdatePads)) {
        LOGE("libGTASA.so is not GTA SA 2.10 arm64-v8a; mod disabled");
        return;
    }

    WriteDefaultIfMissing();
    LoadConfig();

    aml->Hook((void *)(base + FP_KeyEvent.off), (void *)hook_KeyEvent, (void **)&orig_KeyEvent);
    aml->Hook((void *)(base + FP_UpdatePads.off), (void *)hook_UpdatePads, (void **)&orig_UpdatePads);
    InstallAll(base, std::make_integer_sequence<int, HOOK_COUNT>{});

    LOGI("keyboard mod ready (%d getter hooks)", HOOK_COUNT);
}
