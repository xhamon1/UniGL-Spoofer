# UniGL Spoofer

A Zygisk module that spoofs **GPU info (OpenGL ES / Vulkan)** and **device identity** per app, so games
unlock higher graphics settings. All code is written from scratch.

**Targets:** works with any Zygisk implementation that supports module API v4
(Magisk, Zygisk Next, ReZygisk, BRZygisk, ...). Android 8.0+, arm64-v8a / x86_64.

> Status: **v0.1, experimental.** The source passes syntax checks but has not been tested on a device yet.
> Please report results (logcat tag `UniGL`) via Issues.

## Why it works on every Zygisk variant

- It only uses the public API v4: `preAppSpecialize`, `postAppSpecialize` and the root companion
  (`connectCompanion`). The config is read by the root companion, not by the game process,
  so SELinux is not a problem.
- Hooking uses a built-in GOT/PLT patcher (`src/hook.cpp`) instead of the implementation-specific
  `pltHookRegister`.
- Non-target apps unload the module immediately (`DLCLOSE_MODULE_LIBRARY`).

## What gets spoofed

| Area | Method |
|---|---|
| `glGetString` (vendor, renderer, version, extra extensions) | GOT hook + `eglGetProcAddress` + `dlsym` |
| Vulkan `deviceName`, `vendorID`, `deviceID`, `driverVersion` | hook `vkGetPhysicalDeviceProperties(2/2KHR)` + `vkGetInstanceProcAddr` |
| `android.os.Build` (MODEL, BRAND, MANUFACTURER, DEVICE, PRODUCT, BOARD, HARDWARE, SOC_*) | JNI static fields |
| `ro.product.*`, `ro.hardware`, `ro.soc.*` | hook `__system_property_get` |

## Build

Push to GitHub and GitHub Actions (`.github/workflows/build.yml`) builds the module zip.
Create a release with a tag: `git tag v0.1.0 && git push --tags`.

Local build (requires the Android NDK):

```sh
for abi in arm64-v8a x86_64; do
  cmake -S . -B build/$abi -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=$abi -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
  cmake --build build/$abi && mkdir -p module/zygisk && cp build/$abi/libunigl.so module/zygisk/$abi.so
done
(cd module && zip -r9 ../UniGL-Spoofer.zip .)
```

## Usage

1. Make sure Zygisk is enabled (any variant), flash the zip, reboot.
2. Open the module's **WebUI** (KernelSU / KernelSU-Next / APatch, or WebUI-X for Magisk).
3. Add the game's package, pick a profile, **force stop** the game and launch it again.
4. Make sure the game is **not** on a denylist / unmount list, otherwise Zygisk will not inject the module.

The config lives in `/data/adb/unigl/` (`targets.conf` and `profiles/*.conf`, editable by hand).
Bundled profiles: `mali-g720`, `build-only-s24u`, `s24ultra`, `template`.

Suggested test order: start with a profile whose GPU vendor matches your device, then `build-only-s24u`,
then cross-vendor profiles. A profile with a different GPU vendor can trigger vendor-specific code paths
in a game (glitches / crashes). The bundled values are only examples; double-check them with a GPU info app.

## Limitations (v0.1)

- 64-bit only. Packed relocations (`DT_ANDROID_RELA`) are not supported yet, so libraries using them are not hooked.
- Does not change CPU core count, RAM or refresh rate.
- Games that check a device list on a server are not affected.
- Spoofing device identity in online games may violate that game's terms of service. Use at your own risk.

## Debugging

`adb logcat -s UniGL` shows `target matched: <package>` followed by `installed: prop=.. gl=.. vk=.. hooks=..`.
If there is no `target matched` line, check `targets.conf`, the denylist and the Zygisk status.

## License

MIT. `src/zygisk.hpp` follows the public Zygisk API v4 (0BSD, John Wu).
