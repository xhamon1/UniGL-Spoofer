#include "profile.h"
#include "spoof.h"
#include "zygisk.hpp"

#include <android/log.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TAG "UniGL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

namespace {

constexpr const char *kConfDir = "/data/adb/unigl";
constexpr uint32_t kMaxBlob = 16384;

bool read_all(int fd, void *buf, size_t n) {
    char *p = (char *)buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

bool write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    while (n) {
        ssize_t r = write(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

bool valid_chars(const char *s, bool allow_dot) {
    if (!*s) return false;
    for (; *s; ++s) {
        char c = *s;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '-' || (allow_dot && c == '.');
        if (!ok) return false;
    }
    return true;
}

bool lookup_profile_name(const char *pkg, char *out, size_t cap) {
    char path[128];
    snprintf(path, sizeof(path), "%s/targets.conf", kConfDir);
    FILE *f = fopen(path, "re");
    if (!f) return false;
    char line[512];
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) {
        char *l = line;
        while (*l == ' ' || *l == '\t') ++l;
        if (*l == '#' || *l == '\n' || !*l) continue;
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char *val = eq + 1;
        size_t kn = strlen(l);
        while (kn && (l[kn - 1] == ' ' || l[kn - 1] == '\t')) l[--kn] = 0;
        size_t vn = strlen(val);
        while (vn && (val[vn - 1] == '\n' || val[vn - 1] == '\r' || val[vn - 1] == ' ')) val[--vn] = 0;
        while (*val == ' ') ++val;
        if (strcmp(l, pkg) == 0 && valid_chars(val, false) && strlen(val) < cap) {
            strcpy(out, val);
            found = true;
        }
    }
    fclose(f);
    return found;
}

void companion_handler(int fd) {
    uint32_t zero = 0;
    uint32_t len = 0;
    if (!read_all(fd, &len, sizeof(len)) || len == 0 || len > 255) { write_all(fd, &zero, 4); return; }
    char pkg[256];
    if (!read_all(fd, pkg, len)) return;
    pkg[len] = 0;
    char prof[64];
    if (!valid_chars(pkg, true) || !lookup_profile_name(pkg, prof, sizeof(prof))) { write_all(fd, &zero, 4); return; }

    char path[160];
    snprintf(path, sizeof(path), "%s/profiles/%s.conf", kConfDir, prof);
    FILE *f = fopen(path, "re");
    if (!f) { write_all(fd, &zero, 4); return; }
    static thread_local char blob[kMaxBlob];
    size_t n = fread(blob, 1, kMaxBlob - 1, f);
    fclose(f);
    blob[n] = 0;
    uint32_t out = (uint32_t)n;
    if (!write_all(fd, &out, 4) || n == 0) return;
    write_all(fd, blob, n);
}

class UniGLModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {

        if ((args->uid % 100000) >= 90000) { unload(); return; }

        char pkg[256] = {0};
        if (args->nice_name) {
            const char *s = env_->GetStringUTFChars(args->nice_name, nullptr);
            if (s) {
                size_t i = 0;
                for (; s[i] && s[i] != ':' && i < sizeof(pkg) - 1; ++i) pkg[i] = s[i];
                env_->ReleaseStringUTFChars(args->nice_name, s);
            }
        }
        if (!pkg[0] || !fetch_profile(pkg)) { unload(); return; }
        active_ = true;
        LOGI("target matched: %s", pkg);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!active_) return;
        Profile p;
        if (profile_parse(blob_, &p)) spoof_install(p, env_);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override { unload(); }

private:
    void unload() { api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY); }

    bool fetch_profile(const char *pkg) {
        int fd = api_->connectCompanion();
        if (fd < 0) return false;
        uint32_t len = (uint32_t)strlen(pkg), n = 0;
        bool ok = write_all(fd, &len, 4) && write_all(fd, pkg, len) && read_all(fd, &n, 4) && n > 0 && n < kMaxBlob &&
                  read_all(fd, blob_, n);
        close(fd);
        if (ok) blob_[n] = 0;
        return ok;
    }

    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool active_ = false;
    char blob_[kMaxBlob] = {0};
};

}

REGISTER_ZYGISK_MODULE(UniGLModule)
REGISTER_ZYGISK_COMPANION(companion_handler)
