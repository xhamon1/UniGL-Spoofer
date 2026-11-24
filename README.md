# UniGL Spoofer

A Zygisk module that spoofs **GPU info (OpenGL ES / Vulkan)** and **device identity** per app, so games
unlock higher graphics settings. All code is written from scratch.

**Compatibility:** works with any Zygisk implementation. Android 8.0+, arm64-v8a / x86_64.

> Status: **v0.1, experimental.** The source passes syntax checks but has not been tested on a device yet.
> Please report results (logcat tag `UniGL`) via Issues.

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
Bundled device profiles (top of the AnTuTu Android ranking, October 2026, global data). Every device comes in two flavors:

- **Minimal**: GPU strings (OpenGL ES renderer/vendor, Vulkan device name), manufacturer and model only. Start here.
- **Full**: everything in Minimal plus Vulkan vendor ID, brand, board, hardware and SoC model/manufacturer.

| # | Device | Model | GPU |
|---|---|---|---|
| 1 | HONOR Magic9 Pro Max | WKL-AN20 | Adreno 850 |
| 2 | RedMagic 11 Pro | NX809J | Adreno 840 |
| 3 | POCO F9 Ultra | 26077PC53G | Adreno 840 |
| 4 | iQOO 15 | I2501 | Adreno 840 |
| 5 | OnePlus 15 | CPH2747 | Adreno 840 |
| 6 | Xiaomi 17 Ultra | 2512BPNDAG | Adreno 840 |
| 7 | POCO F8 Ultra | 25102PCBEG | Adreno 840 |
| 8 | OPPO Find X9 Ultra | CPH2841 | Adreno 840 |
| 9 | Samsung S26 Ultra | SM-S948B | Adreno 840 |

Model numbers come from public listings (some from pre-release Geekbench entries), so double-check them if you can.
Games may not know brand-new GPUs, so Adreno 840 profiles are the safest first choice.

Device profiles cannot be edited or deleted. For anything else use **Custom** in the WebUI: create, edit and delete your own profiles
(you can start from any device profile with "Fill from device"). Every key is optional; an empty value leaves the real value untouched.
Custom profiles live in `/data/adb/unigl/profiles/<name>.conf` and are never touched by module updates.

## Limitations (v0.1)

- Only the app's own libraries are patched (not system libraries). 64-bit only. Packed relocations (`DT_ANDROID_RELA`) are not supported yet, so libraries using them are not hooked.
- Does not change CPU core count, RAM or refresh rate.
- Games that check a device list on a server are not affected.
- Spoofing device identity in online games may violate that game's terms of service. Use at your own risk.

## Restore to normal / uninstall

- **One game only:** open the WebUI, tap **Remove** next to the game, then force stop it. It is back to normal on the next launch, no reboot needed.
- **Everything off:** turn the module off in your root manager and reboot. Nothing is spoofed anymore.
- **Uninstall:** remove the module in your root manager and reboot. Optionally delete your settings too:
  `su -c "rm -rf /data/adb/unigl"`.
- **A game keeps crashing and you cannot open the WebUI:** run
  `su -c "rm /data/adb/unigl/targets.conf"` (or create the file `/data/adb/modules/unigl_spoofer/disable`) and reboot.
  The module only touches the games you listed, so it cannot cause a boot loop.

## Debugging

`adb logcat -s UniGL` shows `target matched: <package>` followed by `installed: prop=.. gl=.. vk=.. hooks=..`.
If there is no `target matched` line, check `targets.conf`, the denylist and the Zygisk status.

## License

MIT. `src/zygisk.hpp` follows the public Zygisk API v4 (0BSD, John Wu).
