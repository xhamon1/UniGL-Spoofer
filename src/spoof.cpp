#include "spoof.h"
#include "hook.h"
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>

#define TAG "UniGL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

namespace {

Profile g_p;

typedef int (*prop_get_fn)(const char *, char *);
typedef const unsigned char *(*gl_get_string_fn)(unsigned);
typedef void *(*egl_gpa_fn)(const char *);
typedef void *(*dlsym_fn)(void *, const char *);
typedef void *(*dlopen_fn)(const char *, int);
typedef void *(*dlopen_ext_fn)(const char *, int, const void *);
typedef void *(*ldr_dlopen_fn)(const char *, int, const void *caller);
typedef void *(*ldr_dlopen_ext_fn)(const char *, int, const void *, const void *caller);
typedef void *(*ldr_dlsym_fn)(void *, const char *, const void *caller);
typedef void *(*vk_gipa_fn)(void *, const char *);
typedef void (*vk_props_fn)(void *, void *);

// Taken from this module's own (unpatched) imports => always the real functions.
dlsym_fn real_dlsym = (dlsym_fn)&dlsym;
dlopen_fn real_dlopen = (dlopen_fn)&dlopen;

dlopen_ext_fn real_dlopen_ext;
ldr_dlopen_fn l_dlopen;
ldr_dlopen_ext_fn l_dlopen_ext;
ldr_dlsym_fn l_dlsym;

prop_get_fn o_prop;
gl_get_string_fn o_gl;
egl_gpa_fn o_egl_gpa;
vk_gipa_fn o_vk_gipa;
vk_props_fn o_vk_props, o_vk_props2, o_vk_props2khr;

const char *nz(const char *s) { return s[0] ? s : nullptr; }

// ---------------------------------------------------------------- system properties
const char *prop_override(const char *name) {
    if (!name) return nullptr;
    if (!strncmp(name, "ro.product.", 11)) {  // ro.product.model, ro.product.vendor.model, ...
        const char *last = strrchr(name, '.') + 1;
        if (!strcmp(last, "model")) return nz(g_p.b_model);
        if (!strcmp(last, "brand")) return nz(g_p.b_brand);
        if (!strcmp(last, "manufacturer")) return nz(g_p.b_manufacturer);
        if (!strcmp(last, "device")) return nz(g_p.b_device);
        if (!strcmp(last, "name")) return nz(g_p.b_product);
        if (!strcmp(last, "board")) return nz(g_p.b_board);
        return nullptr;
    }
    if (!strcmp(name, "ro.hardware")) return nz(g_p.b_hardware);
    if (!strcmp(name, "ro.board.platform")) return nz(g_p.b_board);
    if (!strcmp(name, "ro.soc.model")) return nz(g_p.b_soc_model);
    if (!strcmp(name, "ro.soc.manufacturer")) return nz(g_p.b_soc_manufacturer);
    return nullptr;
}

int h_prop_get(const char *name, char *value) {
    if (const char *ov = prop_override(name)) {
        size_t n = strlen(ov);
        if (n >= PROP_VALUE_MAX) n = PROP_VALUE_MAX - 1;
        memcpy(value, ov, n);
        value[n] = 0;
        return (int)n;
    }
    return o_prop ? o_prop(name, value) : 0;
}

// ---------------------------------------------------------------- OpenGL ES / EGL
void resolve_gl() {
    if (void *h = real_dlopen("libGLESv2.so", RTLD_NOW))
        o_gl = (gl_get_string_fn)real_dlsym(h, "glGetString");
}

const unsigned char *ext_with_append(const unsigned char *orig) {
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    static const unsigned char *last = nullptr;
    static char *buf = nullptr;
    pthread_mutex_lock(&m);
    if (orig != last) {  // old buffers are intentionally leaked: other threads may still read them
        size_t a = strlen((const char *)orig), b = strlen(g_p.gl_ext_append);
        buf = (char *)malloc(a + b + 2);
        memcpy(buf, orig, a);
        buf[a] = ' ';
        memcpy(buf + a + 1, g_p.gl_ext_append, b + 1);
        last = orig;
    }
    const unsigned char *r = (const unsigned char *)buf;
    pthread_mutex_unlock(&m);
    return r;
}

const unsigned char *h_gl_get_string(unsigned name) {
    if (!o_gl) resolve_gl();
    const unsigned char *r = o_gl ? o_gl(name) : nullptr;
    if (!r) return r;  // no current context
    switch (name) {
        case 0x1F00: if (g_p.gl_vendor[0]) return (const unsigned char *)g_p.gl_vendor; break;
        case 0x1F01: if (g_p.gl_renderer[0]) return (const unsigned char *)g_p.gl_renderer; break;
        case 0x1F02: if (g_p.gl_version[0]) return (const unsigned char *)g_p.gl_version; break;
        case 0x1F03: if (g_p.gl_ext_append[0]) return ext_with_append(r); break;
        default: break;
    }
    return r;
}

void *h_egl_gpa(const char *name) {
    if (name && !strcmp(name, "glGetString")) return (void *)h_gl_get_string;
    return o_egl_gpa ? o_egl_gpa(name) : nullptr;
}

// ---------------------------------------------------------------- Vulkan
struct VkPropsHead {  // leading part of VkPhysicalDeviceProperties
    uint32_t apiVersion, driverVersion, vendorID, deviceID;
    int32_t deviceType;
    char deviceName[256];
};
struct VkProps2Head {  // VkPhysicalDeviceProperties2
    int32_t sType;
    void *pNext;
    VkPropsHead properties;
};

void apply_vk(VkPropsHead *p) {
    if (g_p.vk_device_name[0]) {
        strncpy(p->deviceName, g_p.vk_device_name, sizeof(p->deviceName) - 1);
        p->deviceName[sizeof(p->deviceName) - 1] = 0;
    }
    if (g_p.has_vk_vendor) p->vendorID = g_p.vk_vendor_id;
    if (g_p.has_vk_device) p->deviceID = g_p.vk_device_id;
    if (g_p.has_vk_driver) p->driverVersion = g_p.vk_driver_version;
}

void resolve_vk() {
    void *h = real_dlopen("libvulkan.so", RTLD_NOW);
    if (!h) return;
    if (!o_vk_props) o_vk_props = (vk_props_fn)real_dlsym(h, "vkGetPhysicalDeviceProperties");
    if (!o_vk_props2) o_vk_props2 = (vk_props_fn)real_dlsym(h, "vkGetPhysicalDeviceProperties2");
    if (!o_vk_props2khr) o_vk_props2khr = (vk_props_fn)real_dlsym(h, "vkGetPhysicalDeviceProperties2KHR");
}

void h_vk_props(void *dev, void *out) {
    if (!o_vk_props) resolve_vk();
    if (!o_vk_props) return;
    o_vk_props(dev, out);
    if (out) apply_vk((VkPropsHead *)out);
}
void h_vk_props2(void *dev, void *out) {
    if (!o_vk_props2) resolve_vk();
    if (!o_vk_props2) return;
    o_vk_props2(dev, out);
    if (out) apply_vk(&((VkProps2Head *)out)->properties);
}
void h_vk_props2khr(void *dev, void *out) {
    if (!o_vk_props2khr) resolve_vk();
    if (!o_vk_props2khr) return;
    o_vk_props2khr(dev, out);
    if (out) apply_vk(&((VkProps2Head *)out)->properties);
}

void *h_vk_gipa(void *inst, const char *name) {
    void *r = o_vk_gipa ? o_vk_gipa(inst, name) : nullptr;
    if (!r || !name) return r;
    if (!strcmp(name, "vkGetPhysicalDeviceProperties")) { o_vk_props = (vk_props_fn)r; return (void *)h_vk_props; }
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2")) { o_vk_props2 = (vk_props_fn)r; return (void *)h_vk_props2; }
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2KHR")) { o_vk_props2khr = (vk_props_fn)r; return (void *)h_vk_props2khr; }
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (void *)h_vk_gipa;
    return r;
}

// ---------------------------------------------------------------- dlopen / dlsym
// Replacing dlopen/dlsym would change the "caller" bionic uses to pick the linker
// namespace, so forward through the loader entry points with the real caller address.
__attribute__((noinline)) void *h_dlopen(const char *file, int flags) {
    const void *caller = __builtin_return_address(0);
    void *r = l_dlopen ? l_dlopen(file, flags, caller) : real_dlopen(file, flags);
    if (r) got_hook_apply();  // new library => patch its imports too
    return r;
}

__attribute__((noinline)) void *h_dlopen_ext(const char *file, int flags, const void *ext) {
    const void *caller = __builtin_return_address(0);
    void *r = l_dlopen_ext ? l_dlopen_ext(file, flags, ext, caller)
                           : (real_dlopen_ext ? real_dlopen_ext(file, flags, ext) : nullptr);
    if (r) got_hook_apply();
    return r;
}

thread_local bool t_in_dlsym = false;

__attribute__((noinline)) void *h_dlsym(void *handle, const char *sym) {
    const void *caller = __builtin_return_address(0);
    if (t_in_dlsym) return l_dlsym ? l_dlsym(handle, sym, caller) : real_dlsym(handle, sym);
    t_in_dlsym = true;
    void *r = l_dlsym ? l_dlsym(handle, sym, caller) : real_dlsym(handle, sym);
    struct Reset { ~Reset() { t_in_dlsym = false; } } reset;
    if (!r || !sym) return r;
    if (!strcmp(sym, "glGetString")) { if (!o_gl) o_gl = (gl_get_string_fn)r; return (void *)h_gl_get_string; }
    if (!strcmp(sym, "eglGetProcAddress")) { if (!o_egl_gpa) o_egl_gpa = (egl_gpa_fn)r; return (void *)h_egl_gpa; }
    if (!strcmp(sym, "vkGetInstanceProcAddr")) { if (!o_vk_gipa) o_vk_gipa = (vk_gipa_fn)r; return (void *)h_vk_gipa; }
    if (!strcmp(sym, "vkGetPhysicalDeviceProperties")) { if (!o_vk_props) o_vk_props = (vk_props_fn)r; return (void *)h_vk_props; }
    if (!strcmp(sym, "vkGetPhysicalDeviceProperties2")) { if (!o_vk_props2) o_vk_props2 = (vk_props_fn)r; return (void *)h_vk_props2; }
    if (!strcmp(sym, "vkGetPhysicalDeviceProperties2KHR")) { if (!o_vk_props2khr) o_vk_props2khr = (vk_props_fn)r; return (void *)h_vk_props2khr; }
    return r;
}

// ---------------------------------------------------------------- Java android.os.Build
void set_static_string(JNIEnv *env, jclass cls, const char *field, const char *value) {
    if (!value || !value[0]) return;
    jfieldID f = env->GetStaticFieldID(cls, field, "Ljava/lang/String;");
    if (!f) { env->ExceptionClear(); return; }
    jstring s = env->NewStringUTF(value);
    if (!s) { env->ExceptionClear(); return; }
    env->SetStaticObjectField(cls, f, s);
    env->DeleteLocalRef(s);
}

void spoof_java(JNIEnv *env) {
    if (!env) return;
    jclass c = env->FindClass("android/os/Build");
    if (!c) { env->ExceptionClear(); return; }
    set_static_string(env, c, "MODEL", g_p.b_model);
    set_static_string(env, c, "BRAND", g_p.b_brand);
    set_static_string(env, c, "MANUFACTURER", g_p.b_manufacturer);
    set_static_string(env, c, "DEVICE", g_p.b_device);
    set_static_string(env, c, "PRODUCT", g_p.b_product);
    set_static_string(env, c, "BOARD", g_p.b_board);
    set_static_string(env, c, "HARDWARE", g_p.b_hardware);
    set_static_string(env, c, "SOC_MODEL", g_p.b_soc_model);          // API 31+, ignored if absent
    set_static_string(env, c, "SOC_MANUFACTURER", g_p.b_soc_manufacturer);
    env->DeleteLocalRef(c);
}

GotHook g_table[16];
size_t g_table_n = 0;
void add(const char *name, void *fn, int scope = 0) { g_table[g_table_n++] = {name, fn, scope}; }

}  // namespace

void spoof_install(const Profile &p, JNIEnv *env) {
    g_p = p;
    const bool want_prop = g_p.b_model[0] || g_p.b_brand[0] || g_p.b_manufacturer[0] || g_p.b_device[0] ||
                           g_p.b_product[0] || g_p.b_board[0] || g_p.b_hardware[0] || g_p.b_soc_model[0] ||
                           g_p.b_soc_manufacturer[0];
    const bool want_gl = g_p.gl_vendor[0] || g_p.gl_renderer[0] || g_p.gl_version[0] || g_p.gl_ext_append[0];
    const bool want_vk = g_p.vk_device_name[0] || g_p.has_vk_vendor || g_p.has_vk_device || g_p.has_vk_driver;

    if (want_prop) spoof_java(env);
    if (!want_prop && !want_gl && !want_vk) return;

    o_prop = (prop_get_fn)real_dlsym(RTLD_DEFAULT, "__system_property_get");
    real_dlopen_ext = (dlopen_ext_fn)real_dlsym(RTLD_DEFAULT, "android_dlopen_ext");
    l_dlopen = (ldr_dlopen_fn)real_dlsym(RTLD_DEFAULT, "__loader_dlopen");
    l_dlopen_ext = (ldr_dlopen_ext_fn)real_dlsym(RTLD_DEFAULT, "__loader_android_dlopen_ext");
    l_dlsym = (ldr_dlsym_fn)real_dlsym(RTLD_DEFAULT, "__loader_dlsym");
    if (void *egl = real_dlopen("libEGL.so", RTLD_NOW)) o_egl_gpa = (egl_gpa_fn)real_dlsym(egl, "eglGetProcAddress");
    if (void *vk = real_dlopen("libvulkan.so", RTLD_NOW)) o_vk_gipa = (vk_gipa_fn)real_dlsym(vk, "vkGetInstanceProcAddr");

    g_table_n = 0;
    if (want_prop && o_prop) add("__system_property_get", (void *)h_prop_get);
    if (want_gl) {
        add("glGetString", (void *)h_gl_get_string);
        if (o_egl_gpa) add("eglGetProcAddress", (void *)h_egl_gpa);
    }
    if (want_vk) {
        add("vkGetPhysicalDeviceProperties", (void *)h_vk_props);
        add("vkGetPhysicalDeviceProperties2", (void *)h_vk_props2);
        add("vkGetPhysicalDeviceProperties2KHR", (void *)h_vk_props2khr);
        if (o_vk_gipa) add("vkGetInstanceProcAddr", (void *)h_vk_gipa);
    }
    add("dlsym", (void *)h_dlsym);
    add("dlopen", (void *)h_dlopen, 1);
    add("android_dlopen_ext", (void *)h_dlopen_ext, 1);

    // Safety net: if our own imports were ever redirected, hooking again would recurse forever.
    if (real_dlsym == (dlsym_fn)h_dlsym || real_dlopen == (dlopen_fn)h_dlopen) {
        LOGI("self-hook detected, aborting install");
        return;
    }
    got_hook_init(g_table, g_table_n, (void *)&spoof_install);
    got_hook_apply();
    LOGI("installed: prop=%d gl=%d vk=%d hooks=%zu", want_prop, want_gl, want_vk, g_table_n);
}
