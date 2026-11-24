#pragma once
#include <stddef.h>

// Minimal, dependency-free GOT/PLT patcher. It does not rely on any Zygisk
// implementation specific API (pltHookRegister).
struct GotHook {
    const char *name;
    void *replacement;
    // 0 = patch imports of the app's own libraries only (/data/app, /data/data, apk!/lib)
    // 1 = app libraries + the Java library loader (libnativeloader), used to notice new dlopen()s
    int scope;
};

// `self_addr` is any address inside this module; the library containing it is never patched.
void got_hook_init(const GotHook *hooks, size_t count, void *self_addr);

// Walk the loaded ELFs and redirect matching imports. Idempotent and thread-safe;
// call again whenever new libraries were loaded.
void got_hook_apply();
