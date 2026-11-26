#pragma once
#include <stddef.h>

struct GotHook {
    const char *name;
    void *replacement;
    int scope;
};

void got_hook_init(const GotHook *hooks, size_t count, void *self_addr);

void got_hook_apply();
