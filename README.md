# GTA SA 2.10 (arm64) keyboard mod

**Status: written and syntax-checked, NOT built or run on a device.** Expect to
test and adjust. Hook offsets and byte fingerprints were taken from the
`libGTASA.so` you uploaded (GTA SA 2.10, arm64-v8a); the mod disables itself on
any other build.

## Build
1. Get AML (Android Mod Loader) by RusJJ and copy its `mod/` header folder to
   `AML_headers/mod/`. Check that `amlmod.h` provides `MYMOD`, `ON_MOD_PRELOAD`,
   `aml->GetLib(name)` and `aml->Hook(addr, replacement, &original)`. If a name
   differs, adjust `main.cpp` (only the last ~15 lines use AML).
2. `ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk`
   (or build in a GitHub Actions workflow with the NDK).
3. Output: `libs/arm64-v8a/libSAKeyboard.so`.

## Install
Install AML's patched loader per its README (it must support the 64-bit 2.10
build), then put `libSAKeyboard.so` in AML's mods folder. On first launch the
mod writes `keyboard_map.ini` to
`/sdcard/Android/data/com.rockstargames.gtasa/files/`.

## Remapping
Edit `keyboard_map.ini` (one `action=KEY1,KEY2` per line, up to 4 keys per
action) and press **F12** in game to reload. Set `log_keys=1` and watch
`adb logcat -s SAKeyboard` to see raw keycodes.

Default layout: WASD move/steer, Space jump/handbrake, Shift sprint, F enter/exit,
C crouch, Ctrl fire, Alt aim, Q/E weapon prev/next, arrow keys camera,
W/S accelerate/brake, H horn, B look behind, Y/N conversation.

## Things to verify first
- **Key event argument order.** Assumed `(action, keycode, unicode, meta)`;
  `log_keys` shows if the codes look right.
- **Camera direction.** If arrows look inverted, set `camera_invert_x/y=1`.
- **Key delivery.** Android must forward hardware keys to the game's native
  key handler. If `log_keys` prints nothing, that's the first thing to fix.
- Mouse look and mouse buttons are not handled (keys only).
