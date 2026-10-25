// Root ptrace injector (arm64): makes the target process call dlopen(libpath).
// Usage: injector <pid> </path/to/libcheat.so>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <asm/ptrace.h>
#include <elf.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdint.h>

#ifndef NT_ARM_SYSTEM_CALL
#define NT_ARM_SYSTEM_CALL 0x404
#endif

static bool FindPath(pid_t pid, uintptr_t addr, std::string& path) {
    char f[64]; snprintf(f, sizeof f, "/proc/%d/maps", pid);
    FILE* fp = fopen(f, "r"); if (!fp) return false;
    char line[1024]; bool ok = false;
    while (fgets(line, sizeof line, fp)) {
        unsigned long s, e; int n = 0;
        if (sscanf(line, "%lx-%lx %*4s %*x %*s %*d %n", &s, &e, &n) < 2 || n == 0) continue;
        if (addr >= s && addr < e) { path = line + n; while (!path.empty() && (path.back()=='\n'||path.back()==' ')) path.pop_back(); ok = !path.empty(); break; }
    }
    fclose(fp); return ok;
}
static uintptr_t ModuleBase(pid_t pid, const std::string& path) {
    char f[64]; snprintf(f, sizeof f, "/proc/%d/maps", pid);
    FILE* fp = fopen(f, "r"); if (!fp) return 0;
    char line[1024]; uintptr_t best = 0;
    while (fgets(line, sizeof line, fp)) {
        unsigned long s, e; int n = 0;
        if (sscanf(line, "%lx-%lx %*4s %*x %*s %*d %n", &s, &e, &n) < 2 || n == 0) continue;
        std::string p = line + n; while (!p.empty() && (p.back()=='\n'||p.back()==' ')) p.pop_back();
        if (p == path && (best == 0 || s < best)) best = s;
    }
    fclose(fp); return best;
}

static uintptr_t FindByName(pid_t pid, const char* name, std::string& path) {
    char f[64]; snprintf(f, sizeof f, "/proc/%d/maps", pid);
    FILE* fp = fopen(f, "r"); if (!fp) return 0;
    char line[1024]; uintptr_t best = 0;
    while (fgets(line, sizeof line, fp)) {
        unsigned long s, e; int n = 0;
        if (sscanf(line, "%lx-%lx %*4s %*x %*s %*d %n", &s, &e, &n) < 2 || n == 0) continue;
        std::string p = line + n; while (!p.empty() && (p.back()=='\n'||p.back()==' ')) p.pop_back();
        if (p.size() >= strlen(name) && p.compare(p.size() - strlen(name), strlen(name), name) == 0)
            if (best == 0 || s < best) { best = s; path = p; }
    }
    fclose(fp); return best;
}
static bool Regs(pid_t pid, user_pt_regs& r, bool set) {
    iovec iov{&r, sizeof r};
    return ptrace(set ? PTRACE_SETREGSET : PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) == 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: injector <pid> <lib> [trapRVA hex]\n"); return 1; }
    pid_t pid = atoi(argv[1]); const char* lib = argv[2];
    unsigned long trapRva = argc > 3 ? strtoul(argv[3], nullptr, 16) : 0x10FC248;

    // remote dlopen = remote module base + (local dlopen - local module base)
    uintptr_t local = (uintptr_t)&dlopen; std::string mod;
    if (!FindPath(getpid(), local, mod)) { printf("cannot locate dlopen module\n"); return 1; }
    uintptr_t lbase = ModuleBase(getpid(), mod), rbase = ModuleBase(pid, mod);
    if (!lbase || !rbase) { printf("module %s not found in target\n", mod.c_str()); return 1; }
    uintptr_t rdlopen = rbase + (local - lbase);
    printf("dlopen module %s -> remote 0x%lx\n", mod.c_str(), (unsigned long)rdlopen);

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) != 0) { perror("attach"); return 1; }
    int st; waitpid(pid, &st, __WALL);

    user_pt_regs orig{}, r{};
    if (!Regs(pid, orig, false)) { perror("getregs"); return 1; }
    r = orig;

    int nosys = -1; iovec siov{&nosys, sizeof nosys};
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_ARM_SYSTEM_CALL, &siov);   // no syscall restart

    uintptr_t strAddr = (orig.sp - 0x800) & ~0xfUL;
    char mem[64]; snprintf(mem, sizeof mem, "/proc/%d/mem", pid);
    int mfd = open(mem, O_RDWR);
    if (mfd < 0 || pwrite(mfd, lib, strlen(lib) + 1, strAddr) < 0) { perror("write mem"); ptrace(PTRACE_DETACH, pid, 0, 0); return 1; }

    // Return address must live in the game's own library so the linker uses the app namespace.
    // We temporarily patch a `brk #0` there to regain control when dlopen returns.
    std::string ipath; uintptr_t ibase = FindByName(pid, "libil2cpp.so", ipath);
    uintptr_t trap = ibase ? ibase + trapRva : 0;
    unsigned char saved[4] = {0}; const unsigned char brk[4] = {0x00, 0x00, 0x20, 0xd4};
    if (trap) {
        pread(mfd, saved, 4, trap);
        pwrite(mfd, brk, 4, trap);
        printf("trap at 0x%lx (libil2cpp base 0x%lx)\n", (unsigned long)trap, (unsigned long)ibase);
    } else printf("libil2cpp not found, using lr=0 (may fail namespace check)\n");

    r.regs[0] = strAddr; r.regs[1] = 2 /*RTLD_NOW*/; r.regs[30] = trap;
    r.pc = rdlopen; r.sp = (orig.sp - 0x1000) & ~0xfUL;
    Regs(pid, r, true);
    ptrace(PTRACE_CONT, pid, 0, 0);
    waitpid(pid, &st, __WALL);
    user_pt_regs after{}; Regs(pid, after, false);
    printf("stopped: sig=%d pc=0x%llx x0=0x%llx\n", WIFSTOPPED(st) ? WSTOPSIG(st) : -1,
           (unsigned long long)after.pc, (unsigned long long)after.regs[0]);
    if (trap) pwrite(mfd, saved, 4, trap);            // restore original code
    Regs(pid, orig, true);                            // restore
    ptrace(PTRACE_DETACH, pid, 0, 0);
    std::string lp; bool loaded = FindByName(pid, "libcheat.so", lp) != 0;
    printf(loaded ? "OK: libcheat.so is mapped in the game\n" : "FAILED: libcheat.so is NOT mapped\n");
    return loaded ? 0 : 2;
}
