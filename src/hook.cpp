#include "hook.h"
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__LP64__)
#error "UniGL only supports 64-bit ABIs (arm64-v8a, x86_64)"
#endif
#if defined(__aarch64__)
enum : unsigned { kJumpSlot = 1026, kGlobDat = 1025 };
#elif defined(__x86_64__)
enum : unsigned { kJumpSlot = 7, kGlobDat = 6 };
#else
#error "unsupported architecture"
#endif

namespace {

const GotHook *g_hooks = nullptr;
size_t g_count = 0;
uintptr_t g_self_addr = 0;
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
thread_local bool t_busy = false;

int query_prot(uintptr_t addr) {
    FILE *f = fopen("/proc/self/maps", "re");
    if (!f) return -1;
    char line[512];
    int prot = -1;
    while (fgets(line, sizeof(line), f)) {
        unsigned long lo, hi;
        char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perm) != 3) continue;
        if (addr >= lo && addr < hi) {
            prot = 0;
            if (perm[0] == 'r') prot |= PROT_READ;
            if (perm[1] == 'w') prot |= PROT_WRITE;
            if (perm[2] == 'x') prot |= PROT_EXEC;
            break;
        }
    }
    fclose(f);
    return prot;
}

void patch_slot(void **slot, void *repl) {
    if (*slot == repl) return;
    const uintptr_t ps = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t page = (uintptr_t)slot & ~(ps - 1);
    int prot = query_prot((uintptr_t)slot);
    if (prot < 0) return;
    bool writable = prot & PROT_WRITE;
    if (!writable && mprotect((void *)page, ps, prot | PROT_WRITE) != 0) return;
    *slot = repl;
    if (!writable) mprotect((void *)page, ps, prot);
}

void scan_relocs(uintptr_t base, const Elf64_Rela *rel, size_t bytes, const Elf64_Sym *symtab,
                 const char *strtab, bool app_lib) {
    size_t n = bytes / sizeof(Elf64_Rela);
    for (size_t i = 0; i < n; ++i) {
        unsigned type = ELF64_R_TYPE(rel[i].r_info);
        if (type != kJumpSlot && type != kGlobDat) continue;  // data (ABS64) relocations are left alone
        uint32_t si = ELF64_R_SYM(rel[i].r_info);
        if (!si || symtab[si].st_shndx != SHN_UNDEF) continue;
        const char *nm = strtab + symtab[si].st_name;
        for (size_t h = 0; h < g_count; ++h) {
            if (strcmp(nm, g_hooks[h].name) == 0) {
                if (!app_lib && g_hooks[h].scope == 0) break;
                patch_slot((void **)(base + rel[i].r_offset), g_hooks[h].replacement);
                break;
            }
        }
    }
}

bool is_app_lib(const char *n) {
    if (!strncmp(n, "/data/adb/", 10)) return false;  // module / root framework libraries
    return !strncmp(n, "/data/app/", 10) || !strncmp(n, "/data/data/", 11) || !strncmp(n, "/data/user/", 11) ||
           strstr(n, "!/lib/") != nullptr;
}

int phdr_cb(dl_phdr_info *info, size_t, void *) {
    uintptr_t base = info->dlpi_addr;
    const char *name = info->dlpi_name ? info->dlpi_name : "";

    // Never patch this module itself (that would make our own calls to the real dlsym/dlopen
    // jump back into our wrappers => infinite recursion). Detect it by address range.
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) &ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) continue;
        uintptr_t lo = base + ph.p_vaddr, hi = lo + ph.p_memsz;
        if (g_self_addr >= lo && g_self_addr < hi) return 0;
    }
    if (strstr(name, "unigl")) return 0;

    const bool app_lib = is_app_lib(name);
    const bool loader_lib = strstr(name, "libnativeloader") != nullptr;
    if (!app_lib && !loader_lib) return 0;

    const Elf64_Dyn *dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (const Elf64_Dyn *)(base + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    uintptr_t strtab = 0, symtab = 0, jmprel = 0, rela = 0;
    size_t pltsz = 0, relasz = 0;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
            case DT_STRTAB: strtab = dyn->d_un.d_ptr; break;
            case DT_SYMTAB: symtab = dyn->d_un.d_ptr; break;
            case DT_JMPREL: jmprel = dyn->d_un.d_ptr; break;
            case DT_PLTRELSZ: pltsz = dyn->d_un.d_val; break;
            case DT_RELA: rela = dyn->d_un.d_ptr; break;
            case DT_RELASZ: relasz = dyn->d_un.d_val; break;
            default: break;
        }
    }
    if (!strtab || !symtab) return 0;
    // some linkers already relocate these pointers, some do not
    auto fix = [base](uintptr_t p) { return p >= base ? p : base + p; };
    const char *str = (const char *)fix(strtab);
    const Elf64_Sym *sym = (const Elf64_Sym *)fix(symtab);
    if (jmprel && pltsz) scan_relocs(base, (const Elf64_Rela *)fix(jmprel), pltsz, sym, str, app_lib);
    if (rela && relasz) scan_relocs(base, (const Elf64_Rela *)fix(rela), relasz, sym, str, app_lib);
    return 0;
}

} // namespace

void got_hook_init(const GotHook *hooks, size_t count, void *self_addr) {
    g_hooks = hooks;
    g_count = count;
    g_self_addr = (uintptr_t)self_addr;
}

void got_hook_apply() {
    if (!g_hooks || t_busy) return;
    t_busy = true;
    pthread_mutex_lock(&g_lock);
    dl_iterate_phdr(phdr_cb, nullptr);
    pthread_mutex_unlock(&g_lock);
    t_busy = false;
}
