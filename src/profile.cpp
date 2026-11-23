#include "profile.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct StrKey { const char *key; size_t off; size_t cap; };
struct NumKey { const char *key; size_t off; size_t flag_off; };

#define S(k, f) { k, offsetof(Profile, f), sizeof(((Profile *)nullptr)->f) }
#define N(k, f, h) { k, offsetof(Profile, f), offsetof(Profile, h) }

const StrKey kStr[] = {
    S("gl_vendor", gl_vendor), S("gl_renderer", gl_renderer), S("gl_version", gl_version),
    S("gl_extensions_append", gl_ext_append),
    S("vk_device_name", vk_device_name),
    S("build_model", b_model), S("build_brand", b_brand), S("build_manufacturer", b_manufacturer),
    S("build_device", b_device), S("build_product", b_product), S("build_board", b_board),
    S("build_hardware", b_hardware), S("build_soc_model", b_soc_model),
    S("build_soc_manufacturer", b_soc_manufacturer),
};
const NumKey kNum[] = {
    N("vk_vendor_id", vk_vendor_id, has_vk_vendor),
    N("vk_device_id", vk_device_id, has_vk_device),
    N("vk_driver_version", vk_driver_version, has_vk_driver),
};

char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

} // namespace

bool profile_parse(const char *text, Profile *p) {
    memset(p, 0, sizeof(*p));
    bool any = false;
    char line[2304];
    const char *s = text;
    while (*s) {
        const char *e = strchr(s, '\n');
        size_t full = e ? (size_t)(e - s) : strlen(s);
        size_t n = full < sizeof(line) - 1 ? full : sizeof(line) - 1;
        memcpy(line, s, n);
        line[n] = 0;
        s += full + (e ? 1 : 0);

        char *l = trim(line);
        if (!*l || *l == '#') continue;
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(l);
        char *val = trim(eq + 1);

        for (const auto &k : kStr) {
            if (strcmp(k.key, key) != 0) continue;
            char *dst = (char *)p + k.off;
            strncpy(dst, val, k.cap - 1);
            dst[k.cap - 1] = 0;
            any = any || *val;
            break;
        }
        for (const auto &k : kNum) {
            if (strcmp(k.key, key) != 0 || !*val) continue;
            *(uint32_t *)((char *)p + k.off) = (uint32_t)strtoul(val, nullptr, 0);
            *(bool *)((char *)p + k.flag_off) = true;
            any = true;
            break;
        }
    }
    return any;
}
