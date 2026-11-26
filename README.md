# UniGL Spoofer

A Zygisk module that changes the GPU (OpenGL ES / Vulkan) and device info an app sees, so games can offer
graphics settings that are normally limited to specific devices. Works with any Zygisk. Android 8.0+, arm64-v8a and x86_64.

<p align="center"><img src="docs/webui.png" alt="UniGL Spoofer WebUI" width="320"></p>

## Install

1. Flash the zip in your root manager and reboot.
2. Open the module's WebUI (KernelSU, KernelSU-Next, APatch, or WebUI-X on Magisk).
3. Add the app's package name, choose a profile, force stop the app, and start it again.

The app must not be on a denylist or unmount list, otherwise Zygisk skips it.
Settings are stored in `/data/adb/unigl/` (`targets.conf` and `profiles/*.conf`).

## Profiles

Every profile comes in two flavors:

- **Minimal**: GPU strings (OpenGL ES renderer and vendor, Vulkan device name), manufacturer, and model. Start here.
- **Full**: Minimal plus Vulkan vendor ID, brand, board, hardware, and SoC model/manufacturer.

Built-in profiles follow the top of the AnTuTu Android ranking (October 2026, global):

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

Some model numbers come from pre-release listings, so double-check them if you can.
Adreno 840 profiles are the safest first choice since newer GPUs may not be recognized by every app.

Built-in profiles cannot be edited or deleted. Create your own under **Custom profiles** in the WebUI.
Every key is optional, and an empty value leaves the real value untouched. **Profile specs** shows what any profile changes.

## What it changes

| Target | Method |
|---|---|
| `glGetString` (vendor, renderer, version, extra extensions) | GOT hook on `glGetString`, `eglGetProcAddress`, `dlsym` |
| Vulkan device name, vendor ID, device ID, driver version | hook on `vkGetPhysicalDeviceProperties(2/2KHR)` and `vkGetInstanceProcAddr` |
| `android.os.Build` fields | JNI static fields |
| `ro.product.*`, `ro.hardware`, `ro.board.platform`, `ro.soc.*` | hook on `__system_property_get` |

## Limitations

- Only the app's own libraries are patched, not system libraries.
- 64-bit only.
- Libraries that use packed relocations (`DT_ANDROID_RELA`) are not hooked.
- CPU cores, RAM, and refresh rate are not changed.
- Server-side checks cannot be bypassed.
- Spoofing device info in online games may violate their terms of service.

## Uninstall

- **One app:** tap **Remove** next to it in the WebUI and force stop the app. No reboot needed.
- **Everything:** disable or remove the module in your root manager and reboot. To delete the settings too, run `su -c "rm -rf /data/adb/unigl"`.
- **App crashes and the WebUI is not reachable:** run `su -c "rm /data/adb/unigl/targets.conf"` and reboot,
  or create `/data/adb/modules/unigl_spoofer/disable`. Only listed apps are affected, so this cannot cause a boot loop.

## License

MIT. `src/zygisk.hpp` follows the public Zygisk API v4 (0BSD, John Wu).

## Author

Amon, Telegram: [@xhamon](https://t.me/xhamon)
