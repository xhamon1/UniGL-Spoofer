#pragma once
#include <stddef.h>

// Minimal, dependency-free GOT/PLT patcher. It does not rely on any Zygisk
// implementation specific API (pltHookRegister), so it behaves the same on
// Magisk, Zygisk Next, ReZygisk, BRZygisk ...
struct GotHook {
    const char *name;
    void *replacement;
};

// `self_addr` is any address inside this module; that library is never patched.
void got_hook_init(const GotHook *hooks, size_t count, void *self_addr);

// Walk every loaded ELF and redirect matching imports. Idempotent and thread-safe;
// call again whenever new libraries were loaded.
void got_hook_apply();
